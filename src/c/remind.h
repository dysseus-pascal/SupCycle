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
// und Zeitumstellungen hinweg. Ein noch offener Aufschub (siehe unten) wird
// dabei mitgestellt: er liegt im Persist und ueberlebt jeden Neustart.
void remind_schedule(void);

// Wurde die App von einem Erinnerungs-Wecker gestartet? Liefert dann die
// Uhrzeit der RUNDE in Minuten seit Mitternacht, sonst -1. Ein Aufschub
// liefert die Uhrzeit der aufgeschobenen Runde - nicht die Uhrzeit, zu der er
// klopft: die Erinnerung gilt dieser einen Runde, nicht allem, was offen ist.
int remind_launch_minute(void);

// --- Der Aufschub ---
//
// EIN AUFSCHUB GEHOERT ZU EINER RUNDE. "Spaeter" zur Morgenrunde heisst: in
// SC_SNOOZE_MIN Minuten die Morgenrunde nochmal - und nur die. Die
// Mittagsrunde hat ihren eigenen Wecker und ihre eigene Erinnerung; was vom
// Morgen liegen blieb, steht auf dem Heute-Schirm und laesst sich dort
// nachholen, so wie in Drinktervall ein Glas nachgetragen wird.
//
// HOECHSTENS SC_SNOOZE_MAX MAL. Wer dreimal "spaeter" sagt, meint "heute
// nicht"; danach verfaellt die Runde wie beim Wegdruecken.

#define SC_SNOOZE_MIN 15
#define SC_SNOOZE_MAX 3

// Die Runde zu dieser Uhrzeit in SC_SNOOZE_MIN Minuten nochmal. Zaehlt mit;
// ein Aufschub zu einer anderen Runde ersetzt den alten und zaehlt von vorn.
void remind_snooze(int minute);
// Darf diese Runde noch aufgeschoben werden?
bool remind_snooze_left(int minute);
// Wie oft diese Runde schon aufgeschoben wurde.
int remind_snooze_count(int minute);
// Den Aufschub vergessen - nach dem Nehmen oder dem Wegdruecken.
void remind_snooze_clear(void);
