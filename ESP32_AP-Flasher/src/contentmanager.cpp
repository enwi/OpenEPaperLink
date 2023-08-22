#include "contentmanager.h"

// possibility to turn off, to save space if needed
#define CONTENT_QR
#define CONTENT_RSS
#define CONTENT_CAL
#define CONTENT_BUIENRADAR

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <MD5Builder.h>
#include <locale.h>
#ifdef CONTENT_RSS
#include <rssClass.h>
#endif
#include <time.h>

#include <map>

#include "U8g2_for_TFT_eSPI.h"
#include "commstructs.h"
#include "makeimage.h"
#include "newproto.h"
#include "storage.h"
#ifdef CONTENT_QR
#include "qrcode.h"
#endif
#include "content/content.h"
#include "content/image.h"
#include "content/jsontemplate.h"
#include "content/number.h"
#include "content/weather.h"
#include "language.h"
#include "settings.h"
#include "tag_db.h"
#include "util.h"
#include "web.h"

// https://csvjson.com/json_beautifier

void contentRunner() {
    if (config.runStatus == RUNSTATUS_STOP) return;

    time_t now;
    time(&now);

    for (tagRecord *taginfo : tagDB) {
        if (taginfo->RSSI && (now >= taginfo->nextupdate || taginfo->wakeupReason == WAKEUP_REASON_GPIO || taginfo->wakeupReason == WAKEUP_REASON_NFC) && config.runStatus == RUNSTATUS_RUN && Storage.freeSpace() > 31000 && !util::isSleeping(config.sleepTime1, config.sleepTime2)) {
            drawNew(taginfo->mac, (taginfo->wakeupReason == WAKEUP_REASON_GPIO), taginfo);
            taginfo->wakeupReason = 0;
        }

        if (taginfo->expectedNextCheckin > now - 10 && taginfo->expectedNextCheckin < now + 30 && taginfo->pendingIdle == 0 && taginfo->pending == false) {
            int16_t minutesUntilNextUpdate = (taginfo->nextupdate - now) / 60;
            if (minutesUntilNextUpdate > config.maxsleep) {
                minutesUntilNextUpdate = config.maxsleep;
            }
            if (util::isSleeping(config.sleepTime1, config.sleepTime2)) {
                struct tm timeinfo;
                getLocalTime(&timeinfo);
                struct tm nextSleepTimeinfo = timeinfo;
                nextSleepTimeinfo.tm_hour = config.sleepTime2;
                nextSleepTimeinfo.tm_min = 0;
                nextSleepTimeinfo.tm_sec = 0;
                time_t nextWakeTime = mktime(&nextSleepTimeinfo);
                if (nextWakeTime < now) nextWakeTime += 24 * 3600;
                minutesUntilNextUpdate = (nextWakeTime - now) / 60 - 2;
            }
            if (minutesUntilNextUpdate > 1 && (wsClientCount() == 0 || config.stopsleep == 0)) {
                taginfo->pendingIdle = minutesUntilNextUpdate;
                if (taginfo->isExternal == false) {
                    Serial.printf("sleeping for %d more minutes\n", minutesUntilNextUpdate);
                    prepareIdleReq(taginfo->mac, minutesUntilNextUpdate);
                }
            }
        }

        vTaskDelay(1 / portTICK_PERIOD_MS);  // add a small delay to allow other threads to run
    }
}

void checkVars() {
    DynamicJsonDocument cfgobj(500);
    for (tagRecord *tag : tagDB) {
        if (tag->contentMode == 19) {
            deserializeJson(cfgobj, tag->modeConfigJson);
            const String jsonfile = cfgobj["filename"].as<String>();
            if (!util::isEmptyOrNull(jsonfile)) {
                File file = contentFS->open(jsonfile, "r");
                if (file) {
                    const size_t fileSize = file.size();
                    std::unique_ptr<char[]> fileContent(new char[fileSize + 1]);
                    file.readBytes(fileContent.get(), fileSize);
                    file.close();
                    fileContent[fileSize] = '\0';
                    const char *contentPtr = fileContent.get();
                    for (const auto &entry : varDB) {
                        if (entry.second.changed && strstr(contentPtr, entry.first.c_str()) != nullptr) {
                            Serial.println("updating " + jsonfile + " because of var " + entry.first.c_str());
                            tag->nextupdate = 0;
                        }
                    }
                }
                file.close();
            }
        }
        if (tag->contentMode == 21) {
            if (varDB["ap_tagcount"].changed || varDB["ap_ip"].changed || varDB["ap_ch"].changed) {
                tag->nextupdate = 0;
            }
        }
    }
    for (const auto &entry : varDB) {
        if (entry.second.changed) varDB[entry.first].changed = false;
    }
}

/// @brief Draw a counter
/// @param mac Destination mac
/// @param buttonPressed Was the button pressed (true) or not (false)
/// @param taginfo Tag information
/// @param cfgobj Tag config as json object
/// @param filename Filename
/// @param imageParams Image parameters
/// @param nextupdate Next counter update
/// @param nextCheckin Next tag checkin
void drawCounter(const uint8_t mac[8], const bool buttonPressed, tagRecord *&taginfo, JsonObject &cfgobj, String &filename, imgParam &imageParams, const uint32_t nextupdate, const uint16_t nextCheckin) {
    int32_t counter = cfgobj["counter"].as<int32_t>();
    if (buttonPressed) {
        counter = 0;
    }
    drawNumber(filename, counter, (int32_t)cfgobj["thresholdred"], taginfo, imageParams);
    taginfo->nextupdate = nextupdate;
    Content::updateTagImage(filename, mac, (buttonPressed ? 0 : nextCheckin), taginfo, imageParams);
    cfgobj["counter"] = counter + 1;
}

void drawNew(const uint8_t mac[8], const bool buttonPressed, tagRecord *&taginfo) {
    time_t now;
    time(&now);

    const HwType hwdata = getHwType(taginfo->hwType);
    if (hwdata.bpp == 0) {
        taginfo->nextupdate = now + 600;
        wsErr("No definition found for tag type " + String(taginfo->hwType));
        return;
    }

    uint8_t wifimac[8];
    WiFi.macAddress(wifimac);
    memset(&wifimac[6], 0, 2);

    const bool isAp = memcmp(mac, wifimac, 8) == 0;
    if ((taginfo->wakeupReason == WAKEUP_REASON_FIRSTBOOT || taginfo->wakeupReason == WAKEUP_REASON_WDT_RESET) && taginfo->contentMode == 0 && isAp) {
        taginfo->contentMode = 21;
        taginfo->nextupdate = 0;
    }

    char hexmac[17];
    mac2hex(mac, hexmac);
    String filename = "/" + String(hexmac) + ".raw";
#ifdef YELLOW_IPS_AP
    if (isAp) {
        filename = "direct";
    }
#endif

    struct tm time_info;
    getLocalTime(&time_info);
    time_info.tm_hour = time_info.tm_min = time_info.tm_sec = 0;
    time_info.tm_mday++;
    const time_t midnight = mktime(&time_info);

    DynamicJsonDocument doc(500);
    deserializeJson(doc, taginfo->modeConfigJson);
    JsonObject cfgobj = doc.as<JsonObject>();
    char buffer[64];

    wsLog("Updating " + String(hexmac));
    taginfo->nextupdate = now + 60;

    imgParam imageParams;

    imageParams.width = hwdata.width;
    imageParams.height = hwdata.height;
    imageParams.bpp = hwdata.bpp;
    imageParams.rotatebuffer = hwdata.rotatebuffer;

    imageParams.hasRed = false;
    imageParams.dataType = DATATYPE_IMG_RAW_1BPP;
    imageParams.dither = false;
    if (taginfo->hasCustomLUT && taginfo->lut != 1) {
        imageParams.grayLut = true;
    }

    imageParams.invert = false;
    imageParams.symbols = 0;
    imageParams.rotate = taginfo->rotate;

    switch (taginfo->contentMode) {
        case 0:  // Image
            image::draw(now, filename, cfgobj, taginfo, mac, hexmac, imageParams);
            break;

        case 1:  // Today
            date::draw(now, midnight, filename, cfgobj, taginfo, mac, hexmac, imageParams);
            break;

        case 2:  // CountDays
            drawCounter(mac, buttonPressed, taginfo, cfgobj, filename, imageParams, midnight, 15);
            break;

        case 3:  // CountHours
            drawCounter(mac, buttonPressed, taginfo, cfgobj, filename, imageParams, now + 3600, 5);
            break;

        case 4:  // Weather
            weather::draw(now, filename, cfgobj, taginfo, mac, hexmac, imageParams);
            break;

        case 8:  // Forecast
            weather::forecast::draw(now, filename, cfgobj, taginfo, mac, hexmac, imageParams);
            break;

        case 5:  // Firmware

            filename = cfgobj["filename"].as<String>();
            if (!util::isEmptyOrNull(filename) && !cfgobj["#fetched"].as<bool>()) {
                if (prepareDataAvail(filename, DATATYPE_FW_UPDATE, mac, cfgobj["timetolive"].as<int>())) {
                    cfgobj["#fetched"] = true;
                } else {
                    wsErr("Error accessing " + filename);
                }
                cfgobj["filename"] = "";
                taginfo->nextupdate = 3216153600;
                taginfo->contentMode = 0;
            } else {
                taginfo->nextupdate = now + 300;
            }
            break;

        case 7:  // ImageUrl

        {
            const int httpcode = getImgURL(filename, cfgobj["url"], (time_t)cfgobj["#fetched"], imageParams, String(hexmac));
            const int interval = cfgobj["interval"].as<int>();
            if (httpcode == 200) {
                taginfo->nextupdate = now + 60 * (interval < 3 ? 15 : interval);
                Content::updateTagImage(filename, mac, interval, taginfo, imageParams);
                cfgobj["#fetched"] = now;
            } else if (httpcode == 304) {
                taginfo->nextupdate = now + 60 * (interval < 3 ? 15 : interval);
            } else {
                taginfo->nextupdate = now + 300;
            }
            break;
        }

        case 9:  // RSSFeed

            if (getRssFeed(filename, cfgobj["url"], cfgobj["title"], taginfo, imageParams)) {
                const int interval = cfgobj["interval"].as<int>();
                taginfo->nextupdate = now + 60 * (interval < 3 ? 60 : interval);
                Content::updateTagImage(filename, mac, interval, taginfo, imageParams);
            } else {
                taginfo->nextupdate = now + 300;
            }
            break;

        case 10:  // QRcode:

            drawQR(filename, cfgobj["qr-content"], cfgobj["title"], taginfo, imageParams);
            taginfo->nextupdate = now + 12 * 3600;
            Content::updateTagImage(filename, mac, 0, taginfo, imageParams);
            break;

        case 11:  // Calendar:

            if (getCalFeed(filename, cfgobj["apps_script_url"], cfgobj["title"], taginfo, imageParams)) {
                const int interval = cfgobj["interval"].as<int>();
                taginfo->nextupdate = now + 60 * (interval < 3 ? 15 : interval);
                Content::updateTagImage(filename, mac, interval, taginfo, imageParams);
            } else {
                taginfo->nextupdate = now + 300;
            }
            break;

        case 12:  // RemoteAP
            taginfo->nextupdate = 3216153600;
            break;

        case 13:  // SegStatic
            sprintf(buffer, "%-4.4s%-2.2s%-4.4s", cfgobj["line1"].as<const char *>(), cfgobj["line2"].as<const char *>(), cfgobj["line3"].as<const char *>());
            taginfo->nextupdate = 3216153600;
            sendAPSegmentedData(mac, (String)buffer, 0x0000, false, (taginfo->isExternal == false));
            break;

        case 14:  // NFC URL
            taginfo->nextupdate = 3216153600;
            prepareNFCReq(mac, cfgobj["url"].as<const char *>());
            break;

        case 15:  // send gray LUT
            taginfo->nextupdate = 3216153600;
            prepareLUTreq(mac, cfgobj["bytes"]);
            taginfo->hasCustomLUT = true;
            break;

        case 16:  // buienradar
            buienradar::draw(now, filename, cfgobj, taginfo, mac, hexmac, imageParams);
            break;

        case 17:  // tag command
            sendTagCommand(mac, cfgobj["cmd"].as<int>(), (taginfo->isExternal == false));
            cfgobj["filename"] = "";
            taginfo->nextupdate = 3216153600;
            taginfo->contentMode = 0;
            break;

        case 18:  // tag config
            prepareConfigFile(mac, cfgobj);
            cfgobj["filename"] = "";
            taginfo->nextupdate = 3216153600;
            taginfo->contentMode = 0;
            break;

        case 19:  // json template
            JsonTemplate::draw(now, filename, cfgobj, taginfo, mac, hexmac, imageParams);
            break;

        case 20:  // display a copy
            break;

        case 21:  // ap info
            drawAPinfo(filename, cfgobj, taginfo, imageParams);
            Content::updateTagImage(filename, mac, 0, taginfo, imageParams);
            taginfo->nextupdate = 3216153600;
            break;
    }

    taginfo->modeConfigJson = doc.as<String>();
}

void drawNumber(String &filename, int32_t count, int32_t thresholdred, tagRecord *&taginfo, imgParam &imageParams) {
    int32_t countTemp = count;
    count = abs(count);
    if (taginfo->hwType == SOLUM_SEG_UK) {
        imageParams.symbols = 0x00;
        if (count > 19999) {
            sprintf(imageParams.segments, "over  flow");
            return;
        } else if (count > 9999) {
            imageParams.symbols = 0x02;
            sprintf(imageParams.segments, "%04d", count - 10000);
        } else {
            sprintf(imageParams.segments, "%4d", count);
        }
        if (taginfo->contentMode == 3) {
            strcat(imageParams.segments, "  hour");
        } else {
            strcat(imageParams.segments, "  days");
        }
        return;
    }

    TFT_eSprite spr = TFT_eSprite(&tft);

    StaticJsonDocument<512> loc;
    Content::getTemplate(loc, 2, taginfo->hwType);

    Content::initSprite(spr, imageParams.width, imageParams.height, imageParams);
    spr.setTextDatum(MC_DATUM);
    if (countTemp > thresholdred) {
        spr.setTextColor(TFT_RED, TFT_WHITE);
    } else {
        spr.setTextColor(TFT_BLACK, TFT_WHITE);
    }
    String font = loc["fonts"][0].as<String>();
    if (count > 99) font = loc["fonts"][1].as<String>();
    if (count > 999) font = loc["fonts"][2].as<String>();
    if (count > 9999) font = loc["fonts"][3].as<String>();
    spr.loadFont(font, *contentFS);
    spr.drawString(String(count), loc["xy"][0].as<uint16_t>(), loc["xy"][1].as<uint16_t>());
    spr.unloadFont();

    spr2buffer(spr, filename, imageParams);
    spr.deleteSprite();
}

int getImgURL(String &filename, String URL, time_t fetched, imgParam &imageParams, String MAC) {
    // https://images.klari.net/kat-bw29.jpg

    Storage.begin();

    HTTPClient http;
    http.begin(URL);
    http.addHeader("If-Modified-Since", formatHttpDate(fetched));
    http.addHeader("X-ESL-MAC", MAC);
    http.setTimeout(5000);  // timeout in ms
    const int httpCode = http.GET();
    if (httpCode == 200) {
        File f = contentFS->open("/temp/temp.jpg", "w");
        if (f) {
            http.writeToStream(&f);
            f.close();
            jpg2buffer("/temp/temp.jpg", filename, imageParams);
        }
    } else {
        if (httpCode != 304) {
            wsErr("http " + URL + " " + String(httpCode));
        }
    }
    http.end();
    return httpCode;
}

#ifdef CONTENT_RSS
rssClass reader;
#endif

bool getRssFeed(String &filename, String URL, String title, tagRecord *&taginfo, imgParam &imageParams) {
#ifdef CONTENT_RSS
    // https://github.com/garretlab/shoddyxml2

    // http://feeds.feedburner.com/tweakers/nieuws
    // https://www.nu.nl/rss/Algemeen

    const char *url = URL.c_str();
    constexpr const char *tag = "title";
    constexpr const int rssArticleSize = 128;

    TFT_eSprite spr = TFT_eSprite(&tft);
    U8g2_for_TFT_eSPI u8f;
    u8f.begin(spr);

    StaticJsonDocument<512> loc;
    Content::getTemplate(loc, 9, taginfo->hwType);
    Content::initSprite(spr, imageParams.width, imageParams.height, imageParams);

    if (util::isEmptyOrNull(title)) title = "RSS feed";
    Content::drawString(spr, title, loc["title"][0], loc["title"][1], loc["title"][2], TL_DATUM, TFT_BLACK);

    setU8G2Font(loc["font"], u8f);
    u8f.setFontMode(0);
    u8f.setFontDirection(0);
    u8f.setForegroundColor(TFT_BLACK);
    u8f.setBackgroundColor(TFT_WHITE);

    int n = reader.getArticles(url, tag, rssArticleSize, loc["items"]);
    for (int i = 0; i < n; i++) {
        u8f.setCursor(loc["line"][0], loc["line"][1].as<int>() + i * loc["line"][2].as<int>());
        u8f.print(reader.itemData[i]);
    }

    spr2buffer(spr, filename, imageParams);
    spr.deleteSprite();
#endif

    return true;
}

char *epoch_to_display(time_t utc) {
    static char display[6];
    struct tm local_tm;
    localtime_r(&utc, &local_tm);
    time_t now;
    time(&now);
    struct tm now_tm;
    localtime_r(&now, &now_tm);
    if (local_tm.tm_year < now_tm.tm_year ||
        (local_tm.tm_year == now_tm.tm_year && local_tm.tm_mon < now_tm.tm_mon) ||
        (local_tm.tm_year == now_tm.tm_year && local_tm.tm_mon == now_tm.tm_mon && local_tm.tm_mday < now_tm.tm_mday) ||
        (local_tm.tm_hour == 0 && local_tm.tm_min == 0) ||
        difftime(utc, now) >= 86400) {
        strftime(display, sizeof(display), "%d-%m", &local_tm);
    } else {
        strftime(display, sizeof(display), "%H:%M", &local_tm);
    }
    return display;
}

bool getCalFeed(String &filename, String URL, String title, tagRecord *&taginfo, imgParam &imageParams) {
#ifdef CONTENT_CAL
    // google apps scripts method to retrieve calendar
    // see /data/calendar.txt for description

    wsLog("get calendar");

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char dateString[40];
    strftime(dateString, sizeof(dateString), "%d.%m.%Y", &timeinfo);

    HTTPClient http;
    http.begin(URL);
    http.setTimeout(10000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int httpCode = http.GET();
    if (httpCode != 200) {
        wsErr("http error " + String(httpCode));
        return false;
    }

    DynamicJsonDocument doc(5000);
    DeserializationError error = deserializeJson(doc, http.getString());
    if (error) {
        wsErr(error.c_str());
    }
    http.end();

    TFT_eSprite spr = TFT_eSprite(&tft);
    U8g2_for_TFT_eSPI u8f;
    u8f.begin(spr);

    StaticJsonDocument<512> loc;
    Content::getTemplate(loc, 11, taginfo->hwType);
    Content::initSprite(spr, imageParams.width, imageParams.height, imageParams);

    if (util::isEmptyOrNull(title)) title = "Calendar";
    Content::drawString(spr, title, loc["title"][0], loc["title"][1], loc["title"][2], TL_DATUM, TFT_BLACK);
    Content::drawString(spr, dateString, loc["date"][0], loc["date"][1], loc["title"][2], TR_DATUM, TFT_BLACK);

    u8f.setFontMode(0);
    u8f.setFontDirection(0);
    int n = doc.size();
    if (n > loc["items"]) n = loc["items"];
    for (int i = 0; i < n; i++) {
        const JsonObject &obj = doc[i];
        const String eventtitle = obj["title"];
        const time_t starttime = obj["start"];
        const time_t endtime = obj["end"];
        setU8G2Font(loc["line"][3], u8f);
        if (starttime <= now && endtime > now) {
            u8f.setForegroundColor(TFT_WHITE);
            u8f.setBackgroundColor(TFT_RED);
            spr.fillRect(loc["red"][0], loc["red"][1].as<int>() + i * loc["line"][2].as<int>(), loc["red"][2], loc["red"][3], TFT_RED);
        } else {
            u8f.setForegroundColor(TFT_BLACK);
            u8f.setBackgroundColor(TFT_WHITE);
        }
        u8f.setCursor(loc["line"][0], loc["line"][1].as<int>() + i * loc["line"][2].as<int>());
        if (starttime > 0) u8f.print(epoch_to_display(obj["start"]));
        u8f.setCursor(loc["line"][4], loc["line"][1].as<int>() + i * loc["line"][2].as<int>());
        u8f.print(eventtitle);
    }

    spr2buffer(spr, filename, imageParams);
    spr.deleteSprite();
#endif
    return true;
}

void drawQR(String &filename, String qrcontent, String title, tagRecord *&taginfo, imgParam &imageParams) {
#ifdef CONTENT_QR
    TFT_eSprite spr = TFT_eSprite(&tft);
    Storage.begin();

    const char *text = qrcontent.c_str();
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(2)];
    // https://github.com/ricmoo/QRCode
    qrcode_initText(&qrcode, qrcodeData, 2, ECC_MEDIUM, text);

    StaticJsonDocument<512> loc;
    Content::getTemplate(loc, 10, taginfo->hwType);
    Content::initSprite(spr, imageParams.width, imageParams.height, imageParams);
    Content::drawString(spr, title, loc["title"][0], loc["title"][1], loc["title"][2]);

    const int size = qrcode.size;
    const int dotsize = int((imageParams.height - loc["pos"][1].as<int>()) / size);
    const int xpos = loc["pos"][0].as<int>() - dotsize * size / 2;
    const int ypos = loc["pos"][1];

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                spr.fillRect(xpos + x * dotsize, ypos + y * dotsize, dotsize, dotsize, TFT_BLACK);
            }
        }
    }

    spr2buffer(spr, filename, imageParams);
    spr.deleteSprite();
#endif
}

void drawAPinfo(String &filename, JsonObject &cfgobj, tagRecord *&taginfo, imgParam &imageParams) {
    if (taginfo->hwType == SOLUM_SEG_UK) {
        imageParams.symbols = 0x00;
        sprintf(imageParams.segments, "");
        return;
    }

    TFT_eSprite spr = TFT_eSprite(&tft);
    StaticJsonDocument<2048> loc;
    Content::getTemplate(loc, 21, taginfo->hwType);

    Content::initSprite(spr, imageParams.width, imageParams.height, imageParams);
    const JsonArray jsonArray = loc.as<JsonArray>();
    for (const JsonVariant &elem : jsonArray) {
        drawElement(elem, spr);
    }

    spr2buffer(spr, filename, imageParams);
    spr.deleteSprite();
}

bool getJsonTemplateFile(String &filename, String jsonfile, tagRecord *&taginfo, imgParam &imageParams) {
    if (jsonfile.c_str()[0] != '/') {
        jsonfile = "/" + jsonfile;
    }
    File file = contentFS->open(jsonfile, "r");
    if (file) {
        drawJsonStream(file, filename, taginfo, imageParams);
        file.close();
        // contentFS->remove(jsonfile);
        return true;
    }
    return false;
}

/// @brief Extract a variable with the given path from the given json
/// @note Float and double values are rounded to 2 decimal places
/// @param json Json document
/// @param path Path in form of a.b.1.c
/// @return Value as string
String extractValueFromJson(JsonDocument &json, const String &path) {
    JsonVariant currentObj = json.as<JsonVariant>();
    char *segment = strtok(const_cast<char *>(path.c_str()), ".");

    while (segment != NULL) {
        if (currentObj.is<JsonObject>()) {
            currentObj = currentObj.as<JsonObject>()[segment];
        } else if (currentObj.is<JsonArray>()) {
            int index = atoi(segment);
            currentObj = currentObj.as<JsonArray>()[index];
        } else {
            Serial.printf("Invalid JSON structure at path segment: %s\n", segment);
            return "";
        }
        segment = strtok(NULL, ".");
    }

    if (!currentObj.is<int>() && currentObj.is<float>()) {
        return String(currentObj.as<float>(), 2);
    }

    return currentObj.as<String>();
}

/// @brief Replaces json placeholders ({.a.b.1.c}) with variables
class DataInterceptor : public Stream {
   private:
    /// @brief Stream being wrapped
    Stream &_stream;
    /// @brief Json containing variables
    JsonDocument &_variables;
    /// @brief Parsing buffer
    String _buffer;
    /// @brief Buffer size
    const size_t _bufferSize = 32;

   public:
    DataInterceptor(Stream &stream, JsonDocument &variables)
        : _stream(stream), _variables(variables) {
    }

    int available() override {
        return _buffer.length() + _stream.available();
    }

    int read() override {
        findAndReplace();

        if (_buffer.length() > 0) {
            const int data = _buffer[0];
            _buffer.remove(0, 1);
            return data;
        }

        return -1;  // No more data
    }

    int peek() override {
        findAndReplace();
        return _buffer.length() ? _buffer[0] : -1;
    }

    size_t write(uint8_t data) override {
        return _stream.write(data);
    }

   private:
    /// @brief Fill buffer, find and replace json variables
    void findAndReplace() {
        unsigned int len;
        while ((len = _buffer.length()) < _bufferSize) {
            const int data = _stream.read();
            if (data == -1) {
                break;  // No more data to read
            }
            _buffer += (char)data;
        }

        if (len < 4) {
            // There are no variables with less than 4 characters
            return;
        }

        int endIndex = findVar(_buffer, 0);
        if (endIndex == -1) {
            return;
        }

        const String varCleaned = _buffer.substring(1, endIndex - 1);
        String replacement = extractValueFromJson(_variables, varCleaned);

        // Check for operator and second variable
        if (endIndex + 3 < len) {
            const char op = _buffer[endIndex];
            if ((op == '*' || op == '/' || op == '+' || op == '-')) {
                const int endIndex2 = findVar(_buffer, endIndex + 1);
                if (endIndex2 != -1) {
                    const String var2Cleaned = _buffer.substring(endIndex + 2, endIndex2 - 1);
                    const float v2 = extractValueFromJson(_variables, var2Cleaned).toFloat();
                    endIndex = endIndex2;

                    if (op == '*') {
                        replacement = String(replacement.toFloat() * v2, 0);
                    } else if (op == '/') {
                        replacement = abs(v2) > 0.0f ? String(replacement.toFloat() / v2, 0) : "0";
                    } else if (op == '+') {
                        replacement = String(replacement.toFloat() + v2, 0);
                    } else if (op == '-') {
                        replacement = String(replacement.toFloat() - v2, 0);
                    }
                }
            }
        }

        _buffer = replacement + _buffer.substring(endIndex);
    }

    /// @brief Find a var at given start index
    /// @param buffer Buffer to search in
    /// @param index Index to look at
    /// @return Endindex
    int findVar(const String &buffer, const int index) {
        if (buffer[index] != '{' || buffer[index + 1] != '.') {
            return -1;
        }

        return buffer.indexOf("}", index + 2) + 1;
    }
};

bool getJsonTemplateFileExtractVariables(String &filename, String jsonfile, JsonDocument &variables, tagRecord *&taginfo, imgParam &imageParams) {
    if (jsonfile.c_str()[0] != '/') {
        jsonfile = "/" + jsonfile;
    }
    File file = contentFS->open(jsonfile, "r");
    if (file) {
        auto interceptor = DataInterceptor(file, variables);
        drawJsonStream(interceptor, filename, taginfo, imageParams);
        file.close();
        // contentFS->remove(jsonfile);
        return true;
    }
    return false;
}

int getJsonTemplateUrl(String &filename, String URL, time_t fetched, String MAC, tagRecord *&taginfo, imgParam &imageParams) {
    HTTPClient http;
    http.useHTTP10(true);
    http.begin(URL);
    http.addHeader("If-Modified-Since", formatHttpDate(fetched));
    http.addHeader("X-ESL-MAC", MAC);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(5000);
    const int httpCode = http.GET();
    if (httpCode == 200) {
        drawJsonStream(http.getStream(), filename, taginfo, imageParams);
    } else {
        if (httpCode != 304) {
            wsErr("http " + URL + " status " + String(httpCode));
        }
    }
    http.end();
    return httpCode;
}

void drawJsonStream(Stream &stream, String &filename, tagRecord *&taginfo, imgParam &imageParams) {
    TFT_eSprite spr = TFT_eSprite(&tft);
    Content::initSprite(spr, imageParams.width, imageParams.height, imageParams);
    DynamicJsonDocument doc(300);
    if (stream.find("[")) {
        do {
            DeserializationError error = deserializeJson(doc, stream);
            if (error) {
                wsErr("json error " + String(error.c_str()));
                break;
            } else {
                drawElement(doc.as<JsonObject>(), spr);
                doc.clear();
            }
        } while (stream.findUntil(",", "]"));
    }

    spr2buffer(spr, filename, imageParams);
    spr.deleteSprite();
}

void drawElement(const JsonObject &element, TFT_eSprite &spr) {
    if (element.containsKey("text")) {
        const JsonArray &textArray = element["text"];
        const uint16_t align = textArray[5] | 0;
        const uint16_t size = textArray[6] | 0;
        const String bgcolorstr = textArray[7].as<String>();
        const uint16_t bgcolor = (bgcolorstr.length() > 0) ? getColor(bgcolorstr) : TFT_WHITE;
        Content::drawString(spr, textArray[2], textArray[0].as<int>(), textArray[1].as<int>(), textArray[3], align, getColor(textArray[4]), size, bgcolor);
    } else if (element.containsKey("box")) {
        const JsonArray &boxArray = element["box"];
        spr.fillRect(boxArray[0].as<int>(), boxArray[1].as<int>(), boxArray[2].as<int>(), boxArray[3].as<int>(), getColor(boxArray[4]));
    } else if (element.containsKey("line")) {
        const JsonArray &lineArray = element["line"];
        spr.drawLine(lineArray[0].as<int>(), lineArray[1].as<int>(), lineArray[2].as<int>(), lineArray[3].as<int>(), getColor(lineArray[4]));
    } else if (element.containsKey("triangle")) {
        const JsonArray &lineArray = element["triangle"];
        spr.fillTriangle(lineArray[0].as<int>(), lineArray[1].as<int>(), lineArray[2].as<int>(), lineArray[3].as<int>(), lineArray[4].as<int>(), lineArray[5].as<int>(), getColor(lineArray[6]));
    }
}

uint16_t getColor(const String &color) {
    if (color == "0" || color == "white") return TFT_WHITE;
    if (color == "1" || color == "" || color == "black") return TFT_BLACK;
    if (color == "2" || color == "red") return TFT_RED;
    uint16_t r, g, b;
    if (color.length() == 7 && color[0] == '#' &&
        sscanf(color.c_str(), "#%2hx%2hx%2hx", &r, &g, &b) == 3) {
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
    return TFT_WHITE;
}

char *formatHttpDate(const time_t t) {
    static char buf[40];
    struct tm *timeinfo;
    timeinfo = localtime(&t);                 // Get the local time
    const time_t utcTime = mktime(timeinfo);  // Convert to UTC
    timeinfo = gmtime(&utcTime);
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", timeinfo);
    return buf;
}

String urlEncode(const char *msg) {
    constexpr const char *hex = "0123456789ABCDEF";
    String encodedMsg = "";

    while (*msg != '\0') {
        if (
            ('a' <= *msg && *msg <= 'z') || ('A' <= *msg && *msg <= 'Z') || ('0' <= *msg && *msg <= '9') || *msg == '-' || *msg == '_' || *msg == '.' || *msg == '~') {
            encodedMsg += *msg;
        } else {
            encodedMsg += '%';
            encodedMsg += hex[(unsigned char)*msg >> 4];
            encodedMsg += hex[*msg & 0xf];
        }
        msg++;
    }
    return encodedMsg;
}

void prepareNFCReq(const uint8_t *dst, const char *url) {
    uint8_t *data;
    size_t len = strlen(url);
    data = new uint8_t[len + 8];

    // TLV
    data[0] = 0x03;  // NDEF message (TLV type)
    data[1] = 4 + len + 1;
    // ndef record
    data[2] = 0xD1;
    data[3] = 0x01;     // well known record type
    data[4] = len + 1;  // payload length
    data[5] = 0x55;     // payload type (URI record)
    data[6] = 0x00;     // URI identifier code (no prepending)

    memcpy(data + 7, reinterpret_cast<const uint8_t *>(url), len);
    len = 7 + len;
    data[len] = 0xFE;
    len = 1 + len;
    prepareDataAvail(data, len, DATATYPE_NFC_RAW_CONTENT, dst);
}

void prepareLUTreq(const uint8_t *dst, const String &input) {
    constexpr const char *delimiters = ", \t";
    constexpr const int maxValues = 76;
    uint8_t waveform[maxValues];
    char *ptr = strtok(const_cast<char *>(input.c_str()), delimiters);
    int i = 0;
    while (ptr != nullptr && i < maxValues) {
        waveform[i++] = static_cast<uint8_t>(strtol(ptr, nullptr, 16));
        ptr = strtok(nullptr, delimiters);
    }
    const size_t waveformLen = sizeof(waveform);
    prepareDataAvail(waveform, waveformLen, DATATYPE_CUSTOM_LUT_OTA, dst);
}

void prepareConfigFile(const uint8_t *dst, const JsonObject &config) {
    struct tagsettings tagSettings;
    tagSettings.settingsVer = 1;
    tagSettings.enableFastBoot = config["fastboot"].as<int>();
    tagSettings.enableRFWake = config["rfwake"].as<int>();
    tagSettings.enableTagRoaming = config["tagroaming"].as<int>();
    tagSettings.enableScanForAPAfterTimeout = config["tagscanontimeout"].as<int>();
    tagSettings.enableLowBatSymbol = config["showlowbat"].as<int>();
    tagSettings.enableNoRFSymbol = config["shownorf"].as<int>();
    tagSettings.customMode = 0;
    tagSettings.fastBootCapabilities = 0;
    tagSettings.minimumCheckInTime = 1;
    tagSettings.fixedChannel = config["fixedchannel"].as<int>();
    tagSettings.batLowVoltage = config["lowvoltage"].as<int>();
    prepareDataAvail((uint8_t *)&tagSettings, sizeof(tagSettings), 0xA8, dst);
}

void setU8G2Font(const String &title, U8g2_for_TFT_eSPI &u8f) {
    if (title == "glasstown_nbp_tf") {
        u8f.setFont(u8g2_font_glasstown_nbp_tf);
    } else if (title == "7x14_tf") {
        u8f.setFont(u8g2_font_7x14_tf);
    } else if (title == "t0_14b_tf") {
        u8f.setFont(u8g2_font_t0_14b_tf);
    }
}
