#pragma once
#include <pebble.h>

// Kalendertage: die Tagesnummer, mit der Plan, Haken und Zyklus rechnen.
//
// EIN TAG IST EIN DATUM, NICHT 86400 SEKUNDEN. Bis 0.15.0 hiess "Tag" die
// Ortsmitternacht in Sekunden seit 1970, ganzzahlig durch 86400 geteilt. Das
// ist kein Kalendertag: oestlich von Greenwich war es der Vortag, und in einer
// Zone, die zwischen UTC+0 und UTC+1 wechselt (London, Lissabon, Dublin),
// hatten Samstag und Sonntag dieselbe Nummer, und im Herbst sprang sie mitten
// am Tag (Audit M1). Jetzt zaehlt das Datum der Ortszeit: Tage vom 01.01.1970
// bis zu diesem Datum, ohne Uhrzeit und ohne Zeitzone dazwischen.
//
// Telefon (src/pkjs/index.js, todayDay) und Boulder zaehlen genauso.

// Tage vom 01.01.1970 bis zum Datum (Monat 1..12). Rechnet in jedem Jahr und
// fuer Daten vor 1970 negativ - days_from_civil von Howard Hinnant.
int32_t kalender_tag_aus_datum(int jahr, int monat, int tag);

// Der Kalendertag, auf den `t` in der Ortszeit der Uhr faellt.
int32_t kalender_tag(time_t t);

// Wann es am Kalendertag `tag` in der Ortszeit `minute` (0..1439) Uhr ist.
//
// NICHT Mitternacht + Minuten * 60: an den Umstellungstagen hat der Tag 23
// oder 25 Stunden, und jede Weckzeit danach laege eine Stunde daneben (Audit
// M2). Gibt es die Uhrzeit an diesem Tag nicht (02:30 beim Sommerzeitbeginn),
// kommt die Zeit, die vor der Umstellung so geheissen haette - also eine
// Stunde spaeter auf der Uhr (03:30). Gibt es sie zweimal (Herbst), kommt eine
// der beiden, nie beide.
time_t kalender_zeit_am(int32_t tag, int minute);
