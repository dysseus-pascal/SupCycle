#pragma once
#include <pebble.h>

// Die Erinnerungen: Wecker aus den Uhrzeiten des Plans.
//
// WAS MAN WISSEN MUSS: ein Pebble-Wakeup startet die App im VORDERGRUND. Es
// gibt keinen stillen Hintergrundlauf - der Worker-Prozess kaeme zwar an die
// Uhr, aber nicht an AppMessage. Zur Erinnerungszeit geht der Schirm also an,
// und genau das ist hier erwuenscht.
//
// Pebble erlaubt hoechstens ACHT geplante Wakeups je App. Bei sechs
// Praeparaten mit verschiedenen Zeiten reicht das fuer heute und einen guten
// Teil von morgen; geplant werden immer die naechsten acht, und bei jedem
// Start neu.

// Alle Wecker neu stellen. Bei jedem Start rufen und nach jeder Aenderung am
// Plan - dann haelt sich der Weckplan selbst aktuell, auch ueber Tagesgrenzen
// und Zeitumstellungen hinweg.
//
// `snooze_at` = 0 fuer keinen Aufschub, sonst der Zeitpunkt, zu dem nochmal
// erinnert werden soll.
void remind_schedule(time_t snooze_at);

// Wurde die App von einem Erinnerungs-Wecker gestartet? Liefert dann die
// Uhrzeit in Minuten seit Mitternacht, sonst -1.
int remind_launch_minute(void);

// Um wie viele Minuten "Spaeter" verschiebt.
#define SC_SNOOZE_MIN 15
