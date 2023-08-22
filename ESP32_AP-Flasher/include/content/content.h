#pragma once

#include <Arduino.h>

#include "../../tag_types.h"
#include "makeimage.h"
#include "newproto.h"
#include "tag_db.h"
#include "truetype.h"
#include "util.h"

namespace Content {

/// @brief
/// @param spr
/// @param w
/// @param h
/// @param imageParams
inline void initSprite(TFT_eSprite &spr, int w, int h, imgParam &imageParams) {
    spr.setColorDepth(8);
    spr.createSprite(w, h);
    spr.setRotation(3);
    if (spr.getPointer() == nullptr) {
        wsErr("low on memory. Fallback to 1bpp");
        util::printLargestFreeBlock();
        spr.setColorDepth(1);
        spr.setBitmapColor(TFT_WHITE, TFT_BLACK);
        imageParams.bufferbpp = 1;
        spr.createSprite(w, h);
    }
    if (spr.getPointer() == nullptr) {
        wsErr("Failed to create sprite");
    }
    spr.fillSprite(TFT_WHITE);
}

void getTemplate(JsonDocument &json, const uint8_t id, const uint8_t hwtype) {
    StaticJsonDocument<80> filter;
    StaticJsonDocument<2048> doc;

    const String idstr = String(id);
    constexpr const char *templateKey = "template";

    char filename[20];
    snprintf(filename, sizeof(filename), "/tagtypes/%02X.json", hwtype);
    File jsonFile = contentFS->open(filename, "r");

    if (jsonFile) {
        filter[templateKey][idstr] = true;
        filter["usetemplate"] = true;
        const DeserializationError error = deserializeJson(doc, jsonFile, DeserializationOption::Filter(filter));
        jsonFile.close();
        if (!error && doc.containsKey(templateKey) && doc[templateKey].containsKey(idstr)) {
            json.set(doc[templateKey][idstr]);
            return;
        }
        if (!error && doc.containsKey("usetemplate")) {
            getTemplate(json, id, doc["usetemplate"]);
            return;
        }
        Serial.println("json error in " + String(filename));
        Serial.println(error.c_str());
    } else {
        Serial.println("Failed to open " + String(filename));
    }
}

/// @brief
/// @param filename
/// @param dst
/// @param nextCheckin
/// @param taginfo
/// @param imageParams
/// @return
inline bool updateTagImage(String &filename, const uint8_t *dst, uint16_t nextCheckin, tagRecord *&taginfo, imgParam &imageParams) {
    if (taginfo->hwType == SOLUM_SEG_UK) {
        sendAPSegmentedData(dst, (String)imageParams.segments, imageParams.symbols, imageParams.invert, (taginfo->isExternal == false));
    } else {
        if (imageParams.hasRed) imageParams.dataType = DATATYPE_IMG_RAW_2BPP;
        prepareDataAvail(filename, imageParams.dataType, dst, nextCheckin);
    }
    return true;
}

inline void replaceVariables(String &format) {
    size_t startIndex = 0;
    size_t openBraceIndex, closeBraceIndex;

    while ((openBraceIndex = format.indexOf('{', startIndex)) != -1 &&
           (closeBraceIndex = format.indexOf('}', openBraceIndex + 1)) != -1) {
        const std::string variableName = format.substring(openBraceIndex + 1, closeBraceIndex).c_str();
        const std::string varKey = "{" + variableName + "}";
        auto var = varDB.find(variableName);
        if (var != varDB.end()) {
            format.replace(varKey.c_str(), var->second.value);
        }
        startIndex = closeBraceIndex + 1;
    }
}

inline uint8_t processFontPath(String &font) {
    if (font == "") return 3;
    if (font == "glasstown_nbp_tf") return 1;
    if (font == "7x14_tf") return 1;
    if (font == "t0_14b_tf") return 1;
    if (font.indexOf('/') == -1) font = "/fonts/" + font;
    if (!font.startsWith("/")) font = "/" + font;
    if (font.endsWith(".vlw")) font = font.substring(0, font.length() - 4);
    if (font.endsWith(".ttf")) return 2;
    return 3;
}

/// @brief
///
/// @param spr
/// @param content
/// @param posx
/// @param posy
/// @param font
/// @param align
/// @param color
/// @param size
/// @param bgcolor
inline void drawString(TFT_eSprite &spr, String content, int16_t posx, int16_t posy, String font, byte align = 0, uint16_t color = TFT_BLACK, uint16_t size = 30, uint16_t bgcolor = TFT_WHITE) {
    // drawString(spr,"test",100,10,"bahnschrift30",TC_DATUM,TFT_RED);
    replaceVariables(content);
    switch (processFontPath(font)) {
        case 1: {
            // u8g2 font
            U8g2_for_TFT_eSPI u8f;
            u8f.begin(spr);
            setU8G2Font(font, u8f);
            u8f.setForegroundColor(color);
            u8f.setBackgroundColor(bgcolor);
            if (align == TC_DATUM) {
                posx -= u8f.getUTF8Width(content.c_str()) / 2;
            }
            if (align == TR_DATUM) {
                posx -= u8f.getUTF8Width(content.c_str());
            }
            u8f.setCursor(posx, posy);
            u8f.print(content);
        } break;
        case 2: {
            // truetype
            time_t t = millis();
            truetypeClass truetype = truetypeClass();
            void *framebuffer = spr.getPointer();
            truetype.setFramebuffer(spr.width(), spr.height(), spr.getColorDepth(), static_cast<uint8_t *>(framebuffer));
            File fontFile = contentFS->open(font, "r");
            if (!truetype.setTtfFile(fontFile)) {
                Serial.println("read ttf failed");
                return;
            }

            truetype.setCharacterSize(size);
            truetype.setCharacterSpacing(0);
            if (align == TC_DATUM) {
                posx -= truetype.getStringWidth(content) / 2;
            }
            if (align == TR_DATUM) {
                posx -= truetype.getStringWidth(content);
            }
            truetype.setTextBoundary(posx, spr.width(), spr.height());
            truetype.setTextColor(spr.color16to8(color), spr.color16to8(color));
            truetype.textDraw(posx, posy, content);
            truetype.end();
            // Serial.println("text: '" + content + "' " + String(millis() - t) + "ms");
        } break;
        case 3: {
            // vlw bitmap font
            spr.setTextDatum(align);
            if (font != "") spr.loadFont(font.substring(1), *contentFS);
            spr.setTextColor(color, bgcolor);
            spr.drawString(content, posx, posy);
            if (font != "") spr.unloadFont();
        }
    }
}

/// @brief
/// @param cfgobj
inline void getLocation(JsonObject &cfgobj) {
    const String lat = cfgobj["#lat"];
    const String lon = cfgobj["#lon"];

    if (util::isEmptyOrNull(lat) || util::isEmptyOrNull(lon)) {
        wsLog("get location");
        StaticJsonDocument<80> filter;
        filter["results"][0]["latitude"] = true;
        filter["results"][0]["longitude"] = true;
        filter["results"][0]["timezone"] = true;
        StaticJsonDocument<1000> doc;
        if (util::httpGetJson("https://geocoding-api.open-meteo.com/v1/search?name=" + urlEncode(cfgobj["location"]) + "&count=1", doc, 5000, &filter)) {
            cfgobj["#lat"] = doc["results"][0]["latitude"].as<String>();
            cfgobj["#lon"] = doc["results"][0]["longitude"].as<String>();
            cfgobj["#tz"] = doc["results"][0]["timezone"].as<String>();
        }
    }
}
};  // namespace Content
