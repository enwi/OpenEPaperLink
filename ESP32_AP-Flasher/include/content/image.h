#pragma once

#include "content.h"
#include "storage.h"

namespace image {
inline void draw(const time_t &now, String &filename, JsonObject &cfgobj, tagRecord *&taginfo, const uint8_t *mac, const char *hexmac, imgParam &imageParams) {
    String configFilename = cfgobj["filename"].as<String>();
    taginfo->nextupdate = 3216153600;
    if (util::isEmptyOrNull(configFilename)) {
        return;
    }

    if (!configFilename.startsWith("/")) {
        configFilename = "/" + configFilename;
    }

    if (contentFS->exists(configFilename)) {
        imageParams.dither = cfgobj["dither"] && cfgobj["dither"] == "1";
        jpg2buffer(configFilename, filename, imageParams);
    } else {
        filename = "/current/" + String(hexmac) + ".raw";
        if (contentFS->exists(filename)) {
            prepareDataAvail(filename, imageParams.dataType, mac, cfgobj["timetolive"].as<int>(), true);
            wsLog("File " + configFilename + " not found, resending image " + filename);
        } else {
            wsErr("File " + configFilename + " not found");
        }
        return;
    }

    if (imageParams.hasRed) {
        imageParams.dataType = DATATYPE_IMG_RAW_2BPP;
    }

    if (!prepareDataAvail(filename, imageParams.dataType, mac, cfgobj["timetolive"].as<int>())) {
        wsErr("Error accessing " + filename);
        return;
    }

    if (cfgobj["delete"].as<String>() == "1") {
        contentFS->remove("/" + configFilename);
    }
}
}  // namespace image
