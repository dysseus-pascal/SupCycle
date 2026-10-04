#pragma once
#include <pebble.h>

// Was der Benutzer auf dem Telefon einstellt und die Uhr sich merkt.
//
// Getrennt von plan.c mit Absicht: der Plan sagt, WAS zu nehmen ist, das hier
// sagt, wie die App sich dabei verhält. Beides in einer Datei hiesse, eine
// Frage zur Darstellung im Planspeicher zu suchen.
//
// ACHTUNG Persist-Nummern: plan.c belegt 1 bis 3 und 8, diese Datei 4,
// remind.c 5 bis 7. Sie liegen in demselben Zahlenraum, auch wenn die
// #defines je Datei stehen - 0.14.0 legte die Hakenzeiten versehentlich auch
// in Fach 4 (siehe plan.c). tools/plan_host_test.c prueft die Belegung.

void prefs_init(void);

// Spielt Pilly beim Abhaken? Voreingestellt ja.
bool prefs_fx(void);
void prefs_set_fx(bool on);
