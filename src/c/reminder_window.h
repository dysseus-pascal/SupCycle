#pragma once
#include <pebble.h>

// Vollbild-Erinnerung: Kapsel, Uhrzeit, was ansteht, Aktionsleiste rechts.
//
// `minute` ist die Uhrzeit der Erinnerung in Minuten seit Mitternacht; -1
// heisst "alles, was heute noch offen ist" (nach einem Aufschub oder wenn die
// App von Hand geoeffnet wurde).
//
// Ist zu dieser Zeit nichts mehr offen, erscheint gar nichts - eine
// Erinnerung an etwas bereits Genommenes waere schlimmer als keine.
//
// RUECKGABE FALSE heisst genau das: es kam kein Fenster. Der Aufrufer muss es
// erfahren, denn wenn ein Wecker die App geoeffnet hat, hat sie dann nichts
// mehr zu suchen - sonst bleibt der Heute-Schirm stehen, auf dem Abgehaktes
// durchgestrichen mitsteht, und das liest sich wie "schon wieder faellig".
bool reminder_window_push(int minute);
