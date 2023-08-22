#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "content.h"
#include "makeimage.h"
#include "storage.h"
#include "tag_db.h"
#include "util.h"

namespace JsonTemplate {
/// @brief Extract a value from a given json from the given path
/// @param json Json to extract value from
/// @param path Path inside json in form of ".a.b.2.c"
/// @return Value string or empty if not found
inline String extractValueFromJson(JsonDocument &json, const String &path) {
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

        // if (isdigit(*segment)) {
        //     int index = atoi(segment);
        //     currentObj = currentObj[index];
        // } else {
        //     currentObj = currentObj[segment];
        // }

        segment = strtok(NULL, ".");
    }

    if (!currentObj.is<int>() && currentObj.is<float>()) {
        return String(currentObj.as<float>(), 2);
    }

    return currentObj.as<String>();
}

class JsonValueReplacer : public Stream {
   private:
    Stream &_stream;
    JsonDocument &_variables;
    String _buffer;
    const size_t _bufferSize = 32;

   public:
    JsonValueReplacer(Stream &stream, JsonDocument &variables)
        : _stream(stream), _variables(variables) {
    }

    int available() override {
        return _buffer.length() + _stream.available();
    }

    int read() override {
        fillBuffer();

        if (_buffer.length() > 0) {
            const int data = _buffer[0];
            _buffer.remove(0, 1);
            return data;
        }

        return -1;  // No more data
    }

    int peek() override {
        fillBuffer();
        return _buffer.length() ? _buffer[0] : -1;
    }

    size_t write(uint8_t data) override {
        return _stream.write(data);
    }

   private:
    void fillBuffer() {
        while (_buffer.length() < _bufferSize) {
            const int data = _stream.read();
            if (data == -1) {
                break;  // No more data to read
            }
            _buffer += (char)data;
        }

        const unsigned int len = _buffer.length();
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

        // Check for second variable
        if (endIndex + 3 < len) {
            const char op = _buffer[endIndex];
            if ((op == '*' || op == '/' || op == '+' || op == '-')) {
                const int endIndex2 = findVar(_buffer, endIndex + 1);
                if (endIndex2 != -1) {
                    const String var2Cleaned = _buffer.substring(endIndex + 2, endIndex2 - 1);
                    const String replacement2 = extractValueFromJson(_variables, var2Cleaned);
                    endIndex = endIndex2;

                    if (op == '*') {
                        replacement = String(replacement.toFloat() * replacement2.toFloat(), 0);
                    } else if (op == '/') {
                        replacement = String(replacement.toFloat() / replacement2.toFloat(), 0);
                    } else if (op == '+') {
                        replacement = String(replacement.toFloat() + replacement2.toFloat(), 0);
                    } else if (op == '-') {
                        replacement = String(replacement.toFloat() - replacement2.toFloat(), 0);
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

inline bool getJsonTemplateFileExtractVariables(String &filename, String jsonfile, JsonDocument &variables, tagRecord *&taginfo, imgParam &imageParams) {
    if (jsonfile.c_str()[0] != '/') {
        jsonfile = "/" + jsonfile;
    }
    File file = contentFS->open(jsonfile, "r");
    if (file) {
        auto interceptor = JsonValueReplacer(file, variables);
        drawJsonStream(interceptor, filename, taginfo, imageParams);
        file.close();
        // contentFS->remove(jsonfile);
        return true;
    }
    return false;
}

inline void draw(const time_t &now, String &filename, JsonObject &cfgobj, tagRecord *&taginfo, const uint8_t *mac, const char *hexmac, imgParam &imageParams) {
    const String configFilename = cfgobj["filename"].as<String>();
    if (!util::isEmptyOrNull(configFilename)) {
        String configUrl = cfgobj["url"].as<String>();
        if (!util::isEmptyOrNull(configUrl)) {
            StaticJsonDocument<1000> json;
            Serial.println("Get json url + file");
            if (util::httpGetJson(configUrl, json, 1000)) {
                if (getJsonTemplateFileExtractVariables(filename, configFilename, json, taginfo, imageParams)) {
                    Content::updateTagImage(filename, mac, cfgobj["interval"].as<int>(), taginfo, imageParams);
                } else {
                    wsErr("error opening file " + configFilename);
                }
                const int interval = cfgobj["interval"].as<int>();
                taginfo->nextupdate = now + 60 * (interval < 3 ? 3 : interval);
            } else {
                taginfo->nextupdate = now + 600;
            }

        } else {
            const bool result = getJsonTemplateFile(filename, configFilename, taginfo, imageParams);
            if (result) {
                Content::updateTagImage(filename, mac, cfgobj["interval"].as<int>(), taginfo, imageParams);
            } else {
                wsErr("error opening file " + configFilename);
            }
            taginfo->nextupdate = 3216153600;
        }
    } else {
        const int httpcode = getJsonTemplateUrl(filename, cfgobj["url"], (time_t)cfgobj["#fetched"], String(hexmac), taginfo, imageParams);
        const int interval = cfgobj["interval"].as<int>();
        if (httpcode == 200) {
            taginfo->nextupdate = now + 60 * (interval < 3 ? 15 : interval);
            Content::updateTagImage(filename, mac, interval, taginfo, imageParams);
            cfgobj["#fetched"] = now;
        } else if (httpcode == 304) {
            taginfo->nextupdate = now + 60 * (interval < 3 ? 15 : interval);
        } else {
            taginfo->nextupdate = now + 600;
        }
    }
}
}  // namespace JsonTemplate