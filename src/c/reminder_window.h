#pragma once
#include <pebble.h>

// Vollbild-Erinnerung: Kapsel, Uhrzeit, was ansteht, Aktionsleiste rechts.
//
// `cookie` ist der Cookie des Weckers (remind.h): er nennt die Uhrzeit der
// Runde und, ob es ein Aufschub ist - nur der kann der Runde von gestern
// gelten (remind_runden_tag).
//
// Ist zu dieser Zeit nichts mehr offen, erscheint gar nichts - eine
// Erinnerung an etwas bereits Genommenes waere schlimmer als keine.
//
// RUECKGABE FALSE heisst genau das: es kam kein Fenster. Der Aufrufer muss es
// erfahren, denn wenn ein Wecker die App geoeffnet hat, hat sie dann nichts
// mehr zu suchen - sonst bleibt der Heute-Schirm stehen, auf dem Abgehaktes
// durchgestrichen mitsteht, und das liest sich wie "schon wieder faellig".
//
// STEHT SCHON EIN ERINNERUNGSFENSTER, kommt die Runde dort dazu: es vibriert
// neu und zeigt beide. Ist das Fenster schon abgehakt und wartet nur noch auf
// das Telefon, zeigt es die neue Runde danach, statt zuzugehen. Rueckgabe
// dann true.
bool reminder_window_push(int32_t cookie);
