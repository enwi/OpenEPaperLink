#pragma once

#include <Arduino.h>

constexpr const int defaultLanguage = 0;

extern const String languageList[];

/*EN English language section*/
extern const String languageEnDaysShort[];
extern const String languageEnDays[];
extern const String languageEnMonth[];
/*END English language section END*/

/*NL Dutch language section*/
extern const String languageNlDaysShort[];
extern const String languageNlDays[];
extern const String languageNlMonth[];
/*END Dutch language section END*/

/*DE German language section*/
extern const String languageDeDaysShort[];
extern const String languageDeDays[];
extern const String languageDeMonth[];
/*END German language section END*/

extern const String* languageDaysShort[];
extern const String* languageDays[];
extern const String* languageMonth[];

extern void updateLanguageFromConfig();
constexpr const int getDefaultLanguage() {
    return defaultLanguage;
}
extern const int getCurrentLanguage();
