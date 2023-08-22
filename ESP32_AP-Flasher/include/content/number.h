#pragma once

#include "content.h"
#include "language.h"

namespace date {
inline void drawDate(String &filename, tagRecord *&taginfo, imgParam &imageParams) {
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    const int month_number = timeinfo.tm_mon;
    const int year_number = timeinfo.tm_year + 1900;

    if (taginfo->hwType == SOLUM_SEG_UK) {
        sprintf(imageParams.segments, "%2d%2d%-2.2s%04d", timeinfo.tm_mday, month_number + 1, languageDays[getCurrentLanguage()][timeinfo.tm_wday], year_number);
        imageParams.symbols = 0x04;
        return;
    }

    StaticJsonDocument<512> loc;
    Content::getTemplate(loc, 1, taginfo->hwType);

    TFT_eSprite spr = TFT_eSprite(&tft);
    Content::initSprite(spr, imageParams.width, imageParams.height, imageParams);

    const auto &date = loc["date"];
    const auto &weekday = loc["weekday"];
    if (date) {
        Content::drawString(spr, languageDays[getCurrentLanguage()][timeinfo.tm_wday], weekday[0], weekday[1], weekday[2], TC_DATUM, TFT_RED);
        Content::drawString(spr, String(timeinfo.tm_mday) + " " + languageMonth[getCurrentLanguage()][timeinfo.tm_mon], date[0], date[1], date[2], TC_DATUM);
    } else {
        const auto &month = loc["month"];
        const auto &day = loc["day"];
        Content::drawString(spr, languageDays[getCurrentLanguage()][timeinfo.tm_wday], weekday[0], weekday[1], weekday[2], TC_DATUM, TFT_BLACK);
        Content::drawString(spr, String(languageMonth[getCurrentLanguage()][timeinfo.tm_mon]), month[0], month[1], month[2], TC_DATUM);
        Content::drawString(spr, String(timeinfo.tm_mday), day[0], day[1], day[2], TC_DATUM, TFT_RED);
    }

    spr2buffer(spr, filename, imageParams);
    spr.deleteSprite();
}

inline void draw(const time_t &now, const time_t &midnight, String &filename, JsonObject &cfgobj, tagRecord *&taginfo, const uint8_t *mac, const char *hexmac, imgParam &imageParams) {
    drawDate(filename, taginfo, imageParams);
    taginfo->nextupdate = midnight;
    Content::updateTagImage(filename, mac, (midnight - now) / 60 - 10, taginfo, imageParams);
}
}  // namespace date
