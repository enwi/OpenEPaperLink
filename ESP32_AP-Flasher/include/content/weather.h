#pragma once

#include "content.h"
#include "language.h"
#include "util.h"
#include "web.h"

/// @brief Current weather
namespace weather {

/// @brief Get a weather icon
/// @param id Icon identifier/index
/// @param isNight Use night icons (true) or not (false)
/// @return Icon string
inline const String getWeatherIcon(const uint8_t id, const bool isNight = false) {
    const String weatherIcons[] = {"\uf00d", "\uf00c", "\uf002", "\uf013", "\uf013", "\uf014", "", "", "\uf014", "", "",
                                   "\uf01a", "", "\uf01a", "", "\uf01a", "\uf017", "\uf017", "", "", "",
                                   "\uf019", "", "\uf019", "", "\uf019", "\uf015", "\uf015", "", "", "",
                                   "\uf01b", "", "\uf01b", "", "\uf01b", "", "\uf076", "", "", "\uf01a",
                                   "\uf01a", "\uf01a", "", "", "\uf064", "\uf064", "", "", "", "",
                                   "", "", "", "", "\uf01e", "\uf01d", "", "", "\uf01e"};
    if (isNight && id <= 3) {
        const String nightIcons[] = {"\uf02e", "\uf083", "\uf086"};
        return nightIcons[id];
    }
    return weatherIcons[id];
}

/// @brief Get a wind direction icon
/// @param degrees Wind direction in degrees ranging from 0 to 360
/// @return Icon string
inline const String getWindDirectionIcon(const int degrees) {
    const String directions[] = {"\uf044", "\uf043", "\uf048", "\uf087", "\uf058", "\uf057", "\uf04d", "\uf088"};
    int index = (degrees + 22) / 45;
    if (index >= 8) {
        index = 0;
    }
    return directions[index];
}

/// @brief
/// @param windSpeed
/// @return
inline int windSpeedToBeaufort(const float windSpeed) {
    constexpr const float speeds[] = {0.3, 1.5, 3.3, 5.5, 8, 10.8, 13.9, 17.2, 20.8, 24.5, 28.5, 32.7};
    constexpr const int numSpeeds = sizeof(speeds) / sizeof(speeds[0]);
    int beaufort = 0;
    for (int i = 0; i < numSpeeds; i++) {
        if (windSpeed >= speeds[i]) {
            beaufort = i + 1;
        }
    }
    return beaufort;
}

inline void drawWeather(String &filename, JsonObject &cfgobj, const tagRecord *taginfo, imgParam &imageParams) {
    wsLog("get weather");

    Content::getLocation(cfgobj);

    const String lat = cfgobj["#lat"];
    const String lon = cfgobj["#lon"];
    const String tz = cfgobj["#tz"];
    String units = "";
    if (cfgobj["units"] == "1") {
        units += "&temperature_unit=fahrenheit&windspeed_unit=mph";
    }

    StaticJsonDocument<1000> doc;
    const bool success = util::httpGetJson("https://api.open-meteo.com/v1/forecast?latitude=" + lat + "&longitude=" + lon + "&current_weather=true&windspeed_unit=ms&timezone=" + tz + units, doc, 5000);
    if (!success) {
        return;
    }

    const auto &currentWeather = doc["current_weather"];
    const double temperature = currentWeather["temperature"].as<double>();
    float windspeed = currentWeather["windspeed"].as<float>();
    int windval = 0;
    const int winddirection = currentWeather["winddirection"].as<int>();
    const bool isNight = currentWeather["is_day"].as<int>() == 0;
    uint8_t weathercode = currentWeather["weathercode"].as<int>();
    if (weathercode > 40) weathercode -= 40;

    const uint8_t beaufort = windSpeedToBeaufort(windspeed);
    if (cfgobj["units"] != "1") {
        windval = beaufort;
    } else {
        windval = int(windspeed);
    }

    doc.clear();

    if (taginfo->hwType == SOLUM_SEG_UK) {
        const String weatherText[] = {"sun", "sun", "sun", "CLDY", "CLDY", "FOG", "", "", "FOG", "", "",
                                      "DRZL", "", "DRZL", "", "DRZL", "ice", "ice", "", "", "",
                                      "rain", "", "rain", "", "rain", "ice", "ice", "", "", "",
                                      "SNOW", "", "SNOW", "", "SNOW", "", "SNOW", "", "", "rain",
                                      "rain", "rain", "", "", "SNOW", "SNOW", "", "", "", "",
                                      "", "", "", "", "STRM", "HAIL", "", "", "HAIL"};
        if (temperature < -9.9) {
            sprintf(imageParams.segments, "%3d^%2d%-4.4s", static_cast<int>(temperature), windval, weatherText[weathercode].c_str());
            imageParams.symbols = 0x00;
        } else {
            sprintf(imageParams.segments, "%3d^%2d%-4.4s", static_cast<int>(temperature * 10), windval, weatherText[weathercode].c_str());
            imageParams.symbols = 0x04;
        }
        return;
    }

    Content::getTemplate(doc, 4, taginfo->hwType);

    TFT_eSprite spr = TFT_eSprite(&tft);
    tft.setTextWrap(false, false);

    Content::initSprite(spr, imageParams.width, imageParams.height, imageParams);
    const auto &location = doc["location"];
    Content::drawString(spr, cfgobj["location"], location[0], location[1], location[2]);
    const auto &wind = doc["wind"];
    Content::drawString(spr, String(windval), wind[0], wind[1], wind[2], TR_DATUM, (beaufort > 4 ? TFT_RED : TFT_BLACK));

    char tmpOutput[5];
    dtostrf(temperature, 2, 1, tmpOutput);
    const auto &temp = doc["temp"];
    Content::drawString(spr, String(tmpOutput), temp[0], temp[1], temp[2], TL_DATUM, (temperature < 0 ? TFT_RED : TFT_BLACK));

    const int iconcolor = (weathercode == 55 || weathercode == 65 || weathercode == 75 || weathercode == 82 || weathercode == 86 || weathercode == 95 || weathercode == 96 || weathercode == 99)
                              ? TFT_RED
                              : TFT_BLACK;
    const auto &icon = doc["icon"];
    Content::drawString(spr, getWeatherIcon(weathercode, isNight), icon[0], icon[1], "/fonts/weathericons.ttf", icon[3], iconcolor, icon[2]);
    const auto &dir = doc["dir"];
    Content::drawString(spr, getWindDirectionIcon(winddirection), dir[0], dir[1], "/fonts/weathericons.ttf", TC_DATUM, TFT_BLACK, dir[2]);
    if (weathercode > 10) {
        const auto &umbrella = doc["umbrella"];
        Content::drawString(spr, "\uf084", umbrella[0], umbrella[1], "/fonts/weathericons.ttf", TC_DATUM, TFT_RED, umbrella[2]);
    }

    spr2buffer(spr, filename, imageParams);
    spr.deleteSprite();
}

inline void draw(const time_t &now, String &filename, JsonObject &cfgobj, tagRecord *&taginfo, const uint8_t *mac, const char *hexmac, imgParam &imageParams) {
    // https://open-meteo.com/
    // https://geocoding-api.open-meteo.com/v1/search?name=eindhoven
    // https://api.open-meteo.com/v1/forecast?latitude=52.52&longitude=13.41&current_weather=true
    // https://github.com/erikflowers/weather-icons

    drawWeather(filename, cfgobj, taginfo, imageParams);
    taginfo->nextupdate = now + 1800;
    Content::updateTagImage(filename, mac, 15, taginfo, imageParams);
}

/// @brief Weather forecast
namespace forecast {
inline void drawForecast(String &filename, JsonObject &cfgobj, const tagRecord *taginfo, imgParam &imageParams) {
    wsLog("get weather");
    Content::getLocation(cfgobj);

    String lat = cfgobj["#lat"];
    String lon = cfgobj["#lon"];
    String tz = cfgobj["#tz"];
    String units = "";
    if (cfgobj["units"] == "1") {
        units += "&temperature_unit=fahrenheit&windspeed_unit=mph";
    }

    DynamicJsonDocument doc(2000);
    const bool success = util::httpGetJson("https://api.open-meteo.com/v1/forecast?latitude=" + lat + "&longitude=" + lon + "&daily=weathercode,temperature_2m_max,temperature_2m_min,precipitation_sum,windspeed_10m_max,winddirection_10m_dominant&windspeed_unit=ms&timeformat=unixtime&timezone=" + tz + units, doc, 5000);
    if (!success) {
        return;
    }

    TFT_eSprite spr = TFT_eSprite(&tft);
    tft.setTextWrap(false, false);

    StaticJsonDocument<512> loc;
    Content::getTemplate(loc, 8, taginfo->hwType);
    Content::initSprite(spr, imageParams.width, imageParams.height, imageParams);

    const auto &location = loc["location"];
    Content::drawString(spr, cfgobj["location"], location[0], location[1], location[2], TL_DATUM, TFT_BLACK);
    const auto &daily = doc["daily"];
    const auto &column = loc["column"];
    const int column1 = column[1].as<int>();
    const auto &day = loc["day"];
    for (uint8_t dag = 0; dag < column[0]; dag++) {
        const time_t weatherday = daily["time"][dag].as<time_t>();
        const struct tm *datum = localtime(&weatherday);

        Content::drawString(spr, String(languageDaysShort[getCurrentLanguage()][datum->tm_wday]), dag * column1 + day[0].as<int>(), day[1], day[2], TC_DATUM, TFT_BLACK);

        uint8_t weathercode = daily["weathercode"][dag].as<int>();
        if (weathercode > 40) weathercode -= 40;

        const int iconcolor = (weathercode == 55 || weathercode == 65 || weathercode == 75 || weathercode == 82 || weathercode == 86 || weathercode == 95 || weathercode == 96 || weathercode == 99)
                                  ? TFT_RED
                                  : TFT_BLACK;
        Content::drawString(spr, getWeatherIcon(weathercode), loc["icon"][0].as<int>() + dag * column1, loc["icon"][1], "/fonts/weathericons.ttf", TC_DATUM, iconcolor, loc["icon"][2]);

        Content::drawString(spr, getWindDirectionIcon(daily["winddirection_10m_dominant"][dag]), loc["wind"][0].as<int>() + dag * column1, loc["wind"][1], "/fonts/weathericons.ttf", TC_DATUM, TFT_BLACK, loc["icon"][2]);

        const int8_t tmin = round(daily["temperature_2m_min"][dag].as<double>());
        const int8_t tmax = round(daily["temperature_2m_max"][dag].as<double>());
        uint8_t wind;
        const int8_t beaufort = windSpeedToBeaufort(daily["windspeed_10m_max"][dag].as<double>());
        if (cfgobj["units"] == "1") {
            wind = daily["windspeed_10m_max"][dag].as<int>();
        } else {
            wind = beaufort;
        }

        spr.loadFont(day[2], *contentFS);

        if (loc["rain"]) {
            const int8_t rain = round(daily["precipitation_sum"][dag].as<double>());
            if (rain > 0) {
                Content::drawString(spr, String(rain) + "mm", dag * column1 + loc["rain"][0].as<int>(), loc["rain"][1], "", TC_DATUM, (rain > 10 ? TFT_RED : TFT_BLACK));
            }
        }

        Content::drawString(spr, String(tmin) + " ", dag * column1 + day[0].as<int>(), day[4], "", TR_DATUM, (tmin < 0 ? TFT_RED : TFT_BLACK));
        Content::drawString(spr, String(" ") + String(tmax), dag * column1 + day[0].as<int>(), day[4], "", TL_DATUM, (tmax < 0 ? TFT_RED : TFT_BLACK));
        Content::drawString(spr, String(wind), dag * column1 + column1 - 10, day[3], "", TR_DATUM, (beaufort > 5 ? TFT_RED : TFT_BLACK));
        spr.unloadFont();
        if (dag > 0) {
            for (int i = loc["line"][0]; i < loc["line"][1]; i += 3) {
                spr.drawPixel(dag * column1, i, TFT_BLACK);
            }
        }
    }

    spr2buffer(spr, filename, imageParams);
    spr.deleteSprite();
}

inline void draw(const time_t &now, String &filename, JsonObject &cfgobj, tagRecord *&taginfo, const uint8_t *mac, const char *hexmac, imgParam &imageParams) {
    drawForecast(filename, cfgobj, taginfo, imageParams);
    taginfo->nextupdate = now + 3600;
    Content::updateTagImage(filename, mac, 15, taginfo, imageParams);
}
}  // namespace forecast
};  // namespace weather

/// @brief Buienradar
namespace buienradar {

inline uint8_t drawBuienradar(String &filename, JsonObject &cfgobj, tagRecord *&taginfo, imgParam &imageParams) {
    uint8_t refresh = 60;
#ifdef CONTENT_BUIENRADAR
    wsLog("get buienradar");

    Content::getLocation(cfgobj);
    HTTPClient http;

    String lat = cfgobj["#lat"];
    String lon = cfgobj["#lon"];
    http.begin("https://gps.buienradar.nl/getrr.php?lat=" + lat + "&lon=" + lon);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(5000);
    int httpCode = http.GET();

    if (httpCode == 200) {
        TFT_eSprite spr = TFT_eSprite(&tft);
        U8g2_for_TFT_eSPI u8f;
        u8f.begin(spr);

        StaticJsonDocument<512> loc;
        Content::getTemplate(loc, 16, taginfo->hwType);
        Content::initSprite(spr, imageParams.width, imageParams.height, imageParams);

        tft.setTextWrap(false, false);

        String response = http.getString();

        Content::drawString(spr, cfgobj["location"], loc["location"][0], loc["location"][1], loc["location"][2]);

        for (int i = 0; i < 295; i += 4) {
            int yCoordinates[] = {110, 91, 82, 72, 62, 56, 52};
            for (int y : yCoordinates) {
                spr.drawPixel(i, y, TFT_BLACK);
            }
        }

        Content::drawString(spr, "Buienradar", loc["title"][0], loc["title"][1], loc["title"][2]);

        const auto &bars = loc["bars"];
        const auto &cols = loc["cols"];
        const int cols0 = cols[0].as<int>();
        const int cols1 = cols[1].as<int>();
        const int cols2 = cols[2].as<int>();
        const String cols3 = cols[3].as<String>();
        const int bars0 = bars[0].as<int>();
        const int bars1 = bars[1].as<int>();
        const int bars2 = bars[2].as<int>();
        for (int i = 0; i < 24; i++) {
            const int startPos = i * 11;
            uint8_t value = response.substring(startPos, startPos + 3).toInt();
            const String timestring = response.substring(startPos + 4, startPos + 9);
            const int minutes = timestring.substring(3).toInt();
            if (value < 70) {
                value = 70;
            } else if (value > 180) {
                value = 180;
            }
            if (value > 70) {
                if (i < 12) {
                    refresh = 5;
                } else if (refresh > 5) {
                    refresh = 15;
                }
            }

            spr.fillRect(i * cols2 + bars0, bars1 - (value - 70), bars2, (value - 70), (value > 130 ? TFT_RED : TFT_BLACK));

            if (minutes % 15 == 0) {
                Content::drawString(spr, timestring, i * cols2 + cols0, cols1, cols3);
            }
        }

        spr2buffer(spr, filename, imageParams);
        spr.deleteSprite();
    } else {
        wsErr("Buitenradar http " + String(httpCode));
    }
    http.end();
#endif
    return refresh;
}

inline void draw(const time_t &now, String &filename, JsonObject &cfgobj, tagRecord *&taginfo, const uint8_t *mac, const char *hexmac, imgParam &imageParams) {
    const uint8_t refresh = drawBuienradar(filename, cfgobj, taginfo, imageParams);
    taginfo->nextupdate = now + refresh * 60;
    Content::updateTagImage(filename, mac, refresh, taginfo, imageParams);
}
}  // namespace buienradar
