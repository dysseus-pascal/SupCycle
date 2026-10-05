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
// SC_VORAUS_TAGE abgesucht sind, und - wenn noetig - ein Wecker zum
// Neuplanen (unten). Ist in SC_VORAUS_TAGE nichts faellig, wird bis zum
// ersten faelligen Tag weitergesucht, hoechstens SC_SUCHE_TAGE weit.
//
// DIE KETTE MUSS SICH SELBST TRAGEN. Gestellt wird nur, wenn die App laeuft,
// und meist laeuft sie, weil ein Wecker sie geoeffnet hat. Bis 0.15.0 wurde
// nur zwei Tage voraus geplant: ein Praeparat "alle 2 Tage" oder ein Zyklus,
// der in die Pause ging, liess danach keinen einzigen Wecker stehen - und die
// Erinnerungen hoerten still auf, bis man die App von Hand oeffnete (Audit
// W-K1).
#define SC_VORAUS_TAGE 60

// EINE LANGE PAUSE HAT TROTZDEM IHRE ERINNERUNG. Ist in SC_VORAUS_TAGE nichts
// faellig, sucht die Planung weiter bis zum ersten faelligen Tag und stellt
// dessen Erinnerung - wie jede, die sich meldet, wenn die Uhr sie verpasst.
// Vorher hing die Kette dann allein am Wecker zum Neuplanen, und der meldet
// sich nicht: war die Uhr um 03:00 aus, oeffnete nichts mehr die App, und die
// Erinnerung nach der Pause fiel aus. So weit reicht die laengste Pause der
// Konfigseite samt Einnahmezeit (je 52 Wochen) und dem laengsten Raster
// (30 Tage).
#define SC_SUCHE_TAGE (2 * 52 * 7 + 30)

// DER WECKER ZUM NEUPLANEN (Keepalive) klopft um SC_NEUPLANEN_MINUTE. Die App
// stellt dann alle Wecker neu und geht sofort wieder zu, ohne Fenster, ohne
// Vibration, ohne Nachricht ans Telefon. Ein verpasster meldet sich nicht
// ("verpasst" gilt nur fuer Erinnerungen).
//
// ER STEHT NUR, WENN SONST LANGE NICHTS KLOPFT: in einer Pause oder bei
// einem Raster ueber einen Tag. Ein Wecker startet die App im Vordergrund
// und verdraengt, was dort gerade laeuft - wer um 03:00 navigiert, landete
// jede Nacht auf dem Zifferblatt. Kommt bis zum 03:00 nach dem naechsten
// eine Erinnerung, plant die ohnehin neu. Nur dort faengt er eine neue
// Zeitzone oder Sommerzeitregel ab; bei taeglichen Praeparaten klopft nach
// einem Flug die erste Erinnerung noch zur alten Zeit, erst sie plant neu.
//
// UND NUR, WENN WIEDER ETWAS FAELLIG WERDEN KANN: bei leerem Plan und nach
// jeder abgelaufenen Kur ohne Pause steht er nicht. Ein neuer Plan kommt nur
// bei offener App - der Wecker braechte ihn nie.
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

// Ist es ein Aufschub?
bool remind_cookie_aufschub(int32_t cookie);

// Hat ein Wecker die App gestartet? Dann true und sein Cookie in `cookie`.
bool remind_launch_cookie(int32_t *cookie);

// Der Wecker mit diesem Cookie hat geklopft - beim Start durch ihn und bei
// offener App, vor remind_schedule zu rufen. Ob ein Wecker schon da war,
// merkt sich remind.c, statt es aus der Uhrzeit zu schliessen: das
// Neustellen kurz vor seiner Zeit schiebt ihn ein paar Sekunden, und ein
// weiteres Neustellen dazwischen liess ihn bis sc-r2 ganz wegfallen.
void remind_geklopft(int32_t cookie);

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
//
// UEBER MITTERNACHT bleibt es die Runde des Vortags: 23:50 aufgeschoben
// klopft um 00:05, zaehlt weiter und gilt dem Vortag. Abgehakt wird sie dann
// nicht mehr - Haken gibt es nur fuer heute (plan.h), und der Haken von heute
// gehoert der Runde 23:50 von heute.

#define SC_SNOOZE_MIN 15
#define SC_SNOOZE_MAX 3

// Der Kalendertag der Runde, an die der Wecker mit diesem Cookie erinnert.
// Ein gewoehnlicher Wecker gilt IMMER der Runde von heute - auch wenn er
// nach einer Reise nach Westen Stunden vor ihrer Uhrzeit klopft. Nur ein
// Aufschub kann der Runde von gestern gelten: dann der Tag, den er sich
// gemerkt hat. Bis 0.15.0 wurde das aus der Uhrzeit geschlossen, und ein
// liegen gebliebener Aufschub machte die heutige Runde zur gestrigen:
// "Genommen" hakte nichts ab, und sie klopfte zur Ortszeit nochmal.
int32_t remind_runden_tag(int32_t cookie);

// Was ein Aufschub wiederbringt. Die Runde `minute` vom Kalendertag `tag`
// gibt ihm Uhrzeit, Cookie und Zaehler; `plaetze` (je Platz des Plans ein
// Bit) sind die Plaetze, die beim Aufschieben noch offen waren - von ALLEN
// Runden im Fenster, die von diesem Tag sind, `plaetze_vortag` die vom Tag
// davor (eine Runde von gestern, deren Aufschub ueber Mitternacht klopfte).
//
// ALLE RUNDEN DES FENSTERS KOMMEN WIEDER. Steht die Erinnerung von 08:00
// unbeantwortet, und 12:30 kommt dazu, zeigt das Fenster beide. Bis sc-r2
// galt "spaeter" dann nur 12:30: Zink und D3 von 08:00 hatten heute keinen
// Wecker mehr, ohne dass es irgendwo stand. Liegen gelassen hat man sie
// nicht bewusst - "spaeter" war die erste Antwort auf sie.
//
// DIE PLAETZE GELTEN, NICHT DIE UHRZEIT. Ueber Mitternacht kennt die Uhr die
// Haken von gestern nicht mehr: ohne die Liste zeigte der Aufschub von 23:50
// um 00:05 auch, was um 23:55 auf dem Heute-Schirm abgehakt wurde - eine
// Erinnerung an Genommenes, schlimmer als keine. Bis Mitternacht schrumpft
// die Liste mit jedem Haken (remind_schedule).
typedef struct {
  int minute;
  int32_t tag;
  uint8_t plaetze;
  uint8_t plaetze_vortag;
} Aufschub;

// Diesen Aufschub in SC_SNOOZE_MIN Minuten wiederbringen. Zaehlt mit; ein
// Aufschub zu einer anderen Runde ersetzt den alten und zaehlt von vorn. Der
// Zaehler ueberlebt das Klopfen des Aufschubs - sonst waere jeder Aufschub
// der erste. Von vorn zaehlt erst eine andere Runde oder ein neuer Tag;
// geloescht wird er beim Abhaken und Wegdruecken, und sobald seine Runde
// heute erledigt ist (remind_schedule).
void remind_snooze(const Aufschub *a);
// Der gemerkte Aufschub. False, wenn keiner da ist oder er seine Plaetze
// nicht kennt (gemerkt von sc-r und frueher): dann gilt seine ganze Runde.
bool remind_aufschub(Aufschub *a);
// Darf die Runde `minute` vom Kalendertag `tag` noch aufgeschoben werden?
bool remind_snooze_left(int minute, int32_t tag);
// Wie oft die Runde `minute` vom Kalendertag `tag` schon aufgeschoben wurde.
int remind_snooze_count(int minute, int32_t tag);
// Den Aufschub dieser Runde vergessen - nach dem Nehmen oder dem
// Wegdruecken. Der Aufschub einer anderen Runde bleibt; -1 vergisst jeden.
void remind_snooze_clear(int minute);
