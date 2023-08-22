#include <Arduino.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>

#include "U8g2_for_TFT_eSPI.h"
#include "makeimage.h"
#include "tag_db.h"

struct contentTypes {
    uint16_t id;
    String name;
    uint16_t tagTypes;
    void (*functionname)();
    String description;
    String optionList;
};

void contentRunner();
void checkVars();
void drawNew(const uint8_t mac[8], const bool buttonPressed, tagRecord *&taginfo);
void drawNumber(String &filename, int32_t count, int32_t thresholdred, tagRecord *&taginfo, imgParam &imageParams);
int getImgURL(String &filename, String URL, time_t fetched, imgParam &imageParams, String MAC);
bool getRssFeed(String &filename, String URL, String title, tagRecord *&taginfo, imgParam &imageParams);
bool getCalFeed(String &filename, String URL, String title, tagRecord *&taginfo, imgParam &imageParams);
void drawQR(String &filename, String qrcontent, String title, tagRecord *&taginfo, imgParam &imageParams);
void drawAPinfo(String &filename, JsonObject &cfgobj, tagRecord *&taginfo, imgParam &imageParams);
bool getJsonTemplateFile(String &filename, String jsonfile, tagRecord *&taginfo, imgParam &imageParams);
extern bool getJsonTemplateFileExtractVariables(String &filename, String jsonfile, JsonDocument &variables, tagRecord *&taginfo, imgParam &imageParams);
int getJsonTemplateUrl(String &filename, String URL, time_t fetched, String MAC, tagRecord *&taginfo, imgParam &imageParams);
void drawJsonStream(Stream &stream, String &filename, tagRecord *&taginfo, imgParam &imageParams);
void drawElement(const JsonObject &element, TFT_eSprite &spr);
uint16_t getColor(const String &color);
char *formatHttpDate(const time_t t);
String urlEncode(const char *msg);
void prepareNFCReq(const uint8_t *dst, const char *url);
void prepareLUTreq(const uint8_t *dst, const String &input);
void prepareConfigFile(const uint8_t *dst, const JsonObject &config);
void setU8G2Font(const String &title, U8g2_for_TFT_eSPI &u8f);
