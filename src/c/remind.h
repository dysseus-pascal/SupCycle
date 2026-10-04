#pragma once
#include <pebble.h>

// Die Erinnerungen: Wecker aus den Uhrzeiten des Plans.
//
// WAS MAN WISSEN MUSS: ein Pebble-Wakeup startet die App im VORDERGRUND. Es
// gibt keinen stillen Hintergrundlauf - der Worker-Prozess kaeme zwar an die
// Uhr, aber nicht an AppMessage. Zur Erinnerungszeit geht der Schirm also an,
// und genau das ist hier erwuenscht.
//
// Pebble erlaubt hoechstens ACHT geplante Wakeups je App. Geplant werden die
// naechsten sieben Erinnerungen, so weit voraus, bis sieben gefunden oder
// SC_VORAUS_TAGE abgesucht sind, und dazu ein Wecker zum Neuplanen (unten).
//
// DIE KETTE MUSS SICH SELBST TRAGEN. Gestellt wird nur, wenn die App laeuft,
// und meist laeuft sie, weil ein Wecker sie geoeffnet hat. Bis 0.15.0 wurde
// nur zwei Tage voraus geplant: ein Praeparat "alle 2 Tage" oder ein Zyklus,
// der in die Pause ging, liess danach keinen einzigen Wecker stehen - und die
// Erinnerungen hoerten still auf, bis man die App von Hand oeffnete (Audit
// W-K1).
#define SC_VORAUS_TAGE 60

// DER WECKER ZUM NEUPLANEN (Keepalive) klopft um SC_NEUPLANEN_MINUTE, jede
// Nacht. Die App stellt dann alle Wecker neu und geht sofort wieder zu, ohne
// Fenster, ohne Vibration, ohne Nachricht ans Telefon. Er faengt ab, was die
// Vorausplanung allein nicht kann: eine neue Zeitzone, eine Sommerzeitregel,
// die die Uhr erst spaeter erfaehrt, und eine Pause, die laenger dauert, als
// voraus gesucht wird. Ein verpasster meldet sich nicht ("verpasst" gilt nur
// fuer Erinnerungen).
#define SC_NEUPLANEN_MINUTE (3 * 60)

// Alle Wecker neu stellen. Bei jedem Start rufen und nach jeder Aenderung am
// Plan - dann haelt sich der Weckplan selbst aktuell, auch ueber Tagesgrenzen
// und Zeitumstellungen hinweg. Ein noch offener Aufschub (siehe unten) wird
// dabei mitgestellt: er liegt im Persist und ueberlebt jeden Neustart.
void remind_schedule(void);

// Was ein Wecker mit diesem Cookie bedeutet: die Uhrzeit der RUNDE in
// Minuten seit Mitternacht, sonst -1 (auch fuer den Wecker zum Neuplanen).
// Ein Aufschub liefert die Uhrzeit der aufgeschobenen Runde - nicht die
// Uhrzeit, zu der er klopft: die Erinnerung gilt dieser einen Runde, nicht
// allem, was offen ist.
int remind_cookie_minute(int32_t cookie);

// Ist es der Wecker zum Neuplanen?
bool remind_cookie_neuplanen(int32_t cookie);

// Hat ein Wecker die App gestartet? Dann true und sein Cookie in `cookie`.
bool remind_launch_cookie(int32_t *cookie);

// Wurde die App von einem Erinnerungs-Wecker gestartet? Liefert dann
// remind_cookie_minute seines Cookies, sonst -1.
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
