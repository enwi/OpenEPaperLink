#include "language.h"

#include <Arduino.h>

#include "settings.h"
#include "tag_db.h"

const String languageList[] = {"EN - English", "NL - Nederlands", "DE - Deutsch"};

/*EN English language section*/
const String languageEnDaysShort[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
const String languageEnDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const String languageEnMonth[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
/*END English language section END*/

/*NL Dutch language section*/
const String languageNlDaysShort[] = {"ZO", "MA", "DI", "WO", "DO", "VR", "ZA"};
const String languageNlDays[] = {"zondag", "maandag", "dinsdag", "woensdag", "donderdag", "vrijdag", "zaterdag"};
const String languageNlMonth[] = {"januari", "februari", "maart", "april", "mei", "juni", "juli", "augustus", "september", "oktober", "november", "december"};
/*END Dutch language section END*/

/*DE German language section*/
const String languageDeDaysShort[] = {"SO", "MO", "DI", "MI", "DO", "FR", "SA"};
const String languageDeDays[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
const String languageDeMonth[] = {"Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember"};
/*END German language section END*/

const String* languageDaysShort[] = {languageEnDaysShort, languageNlDaysShort, languageDeDaysShort};
const String* languageDays[] = {languageEnDays, languageNlDays, languageDeDays};
const String* languageMonth[] = {languageEnMonth, languageNlMonth, languageDeMonth};

int currentLanguage = defaultLanguage;

void updateLanguageFromConfig() {
    int tempLang = config.language;
    if (tempLang < 0 || tempLang >= sizeof(languageList)) {
        Serial.println("Language not supported");
        return;
    }
    currentLanguage = tempLang;
}

const int getCurrentLanguage() {
    return currentLanguage;
}
