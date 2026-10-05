// Der Weckplan (remind.c) - auf dem Rechner, mit einer Wecker-Attrappe, die
// sich verhaelt wie pebbleos services/wakeup/service.c (8 je App, eine
// Minute Abstand, keiner in der Vergangenheit).
//
//   sh tools/remind_host_test.sh    (laeuft in Zuerich, London, New York)
//
// Was hier leicht falsch und teuer ist:
//
//   - DIE KETTE TRAEGT SICH SELBST (Audit W-K1). Bis 0.15.0 wurden nur zwei
//     Tage voraus geplant: "alle 2 Tage", um 10 Uhr gestellt, ergab keinen
//     einzigen Wecker, und die Erinnerungen hoerten still auf.
//   - DER WECKER ZUM NEUPLANEN steht nur, wenn sonst lange nichts klopft
//     (Pause, Raster ueber einen Tag): er startet die App im Vordergrund und
//     verdraengt, was um drei dort laeuft. Verpasst meldet er sich nicht.
//     Bei leerem Plan und nach beendeter Kur steht er gar nicht.
//   - EINE LANGE PAUSE haengt nicht allein an ihm: die erste Erinnerung
//     danach steht auch, wenn sie weiter als 60 Tage voraus liegt - bis
//     dahin riss die Kette, wenn die Uhr um 03:00 aus war.
//   - DIE UHRZEIT GILT AM UMSTELLUNGSTAG (Audit M2): 08:00 am Sonntag der
//     Umstellung ist 08:00 auf der Uhr, nicht 07:00 oder 09:00.
//   - HOECHSTENS DREI AUFSCHUEBE. Bis 0.15.0 loeschte das Neustellen nach
//     dem Klopfen des Aufschubs den Zaehler mit - jeder war wieder der erste.
//   - KURZ DAVOR NEU GESTELLT. Lag ein Wecker weniger als 30 s voraus, fiel
//     er bis 0.15.0 beim Neustellen weg (App um 08:14:45 geoeffnet); sc-r
//     schob ihn um bis zu 31 s und verlor ihn beim naechsten Neustellen.
//     Ob er geklopft hat, sagt remind_geklopft, nicht die Uhrzeit.
//   - UEBER MITTERNACHT. Der Aufschub von 23:50 klopft um 00:05 und gilt der
//     Runde von gestern; bis 0.15.0 klopfte er nie.
//   - EINE ANDERE RUNDE laesst den Aufschub stehen.
//
// Die Erwartungen kommen aus der C-Bibliothek (mktime, localtime), nicht aus
// kalender.c.
//
// Exitcode 0 = alles wie zugesagt.
#define _DEFAULT_SOURCE
#include <pebble.h>
#include <stdlib.h>
#include "kalender.h"
#include "plan.h"
#include "remind.h"

time_t stub_jetzt;

static int s_fehler;
static void pruefe(const char *was, bool ok) {
  printf("%s %s\n", ok ? "  ok    " : "  FEHLER", was);
  if (!ok) s_fehler++;
}

#define COOKIE_NEUPLANEN 5000   // remind.c - die Uhr liest ihn beim Start
#define COOKIE_AUFSCHUB 2000
// Die Faecher des Aufschubs, wie 0.15.0 sie hinterliess (remind.c).
#define FACH_AUFSCHUB_ZEIT   5
#define FACH_AUFSCHUB_MINUTE 6
#define FACH_AUFSCHUB_ZAHL   7
#define FACH_AUFSCHUB_TAG    10

static time_t ortszeit(int j, int mo, int t, int h, int mi) {
  struct tm tm = { .tm_year = j - 1900, .tm_mon = mo - 1, .tm_mday = t,
                   .tm_hour = h, .tm_min = mi, .tm_isdst = -1 };
  return mktime(&tm);
}
static int32_t datum_tag(int j, int mo, int t) {
  struct tm tm = { .tm_year = j - 1900, .tm_mon = mo - 1, .tm_mday = t };
  return (int32_t)(timegm(&tm) / 86400);
}
// Liegt `t` in der Ortszeit am Datum (normalisiert: Tag 32 ist der 1. des
// Folgemonats) um hh:mm?
static bool ist_um(time_t t, int j, int mo, int tag, int h, int mi) {
  struct tm soll = { .tm_year = j - 1900, .tm_mon = mo - 1, .tm_mday = tag, .tm_hour = 12, .tm_isdst = -1 };
  mktime(&soll);
  const struct tm ist = *localtime(&t);
  return ist.tm_year == soll.tm_year && ist.tm_mon == soll.tm_mon && ist.tm_mday == soll.tm_mday &&
         ist.tm_hour == h && ist.tm_min == mi;
}
static void zeige(const char *wo) {
  printf("           %s: %d Wecker\n", wo, attrappe_wecker_zahl());
  for (int i = 0; i < attrappe_wecker_zahl(); i++) {
    const AttrappeWecker *w = attrappe_wecker(i);
    char s[32];
    strftime(s, sizeof(s), "%Y-%m-%d %H:%M %Z", localtime(&w->zeit));
    printf("             %s  Cookie %d%s\n", s, (int)w->cookie, w->melden ? "" : "  (still)");
  }
}

// Ein Platz im 26-Byte-Format (plan.h), von Hand.
typedef struct { const char *name; int h, mi, every, on, off; int32_t anker; } Platz;
static void plan_setzen(const Platz *p, int n) {
  uint8_t b[SC_MAX_ITEMS * SC_ITEM_BYTES];
  memset(b, 0, sizeof(b));
  for (int i = 0; i < n; i++) {
    uint8_t *q = b + i * SC_ITEM_BYTES;
    memcpy(q, p[i].name, strlen(p[i].name));
    q[16] = (uint8_t)p[i].h; q[17] = (uint8_t)p[i].mi; q[18] = 1;
    q[19] = (uint8_t)p[i].every; q[20] = (uint8_t)p[i].on; q[21] = (uint8_t)p[i].off;
    for (int k = 0; k < 4; k++) q[22 + k] = (uint8_t)((uint32_t)p[i].anker >> (8 * k));
  }
  plan_set_from_bytes(b, sizeof(b));
}
// Frischer Start um `jetzt`: leerer Persist, keine Wecker, kein Plan.
static void frisch(time_t jetzt) {
  attrappe_persist_leeren();
  attrappe_wecker_leeren();
  attrappe_log_leeren();
  stub_jetzt = jetzt;
  plan_init();
  // plan_init laesst einen Plan im Speicher stehen, wenn das Fach leer ist.
  plan_setzen(NULL, 0);
}
static int erinnerungen(void) {
  int n = 0;
  for (int i = 0; i < attrappe_wecker_zahl(); i++) {
    if (attrappe_wecker(i)->cookie >= 0 && attrappe_wecker(i)->cookie < 1440) n++;
  }
  return n;
}
// Die k-te Erinnerung nach Zeit (ohne Aufschub und Neuplanen).
static const AttrappeWecker *erinnerung(int k) {
  for (int i = 0; i < attrappe_wecker_zahl(); i++) {
    if (attrappe_wecker(i)->cookie >= 0 && attrappe_wecker(i)->cookie < 1440 && k-- == 0) return attrappe_wecker(i);
  }
  return NULL;
}
static const AttrappeWecker *erste_erinnerung(void) { return erinnerung(0); }
// Steht ein Aufschub-Wecker fuer diese Runde?
static bool aufschub_wecker_um(int minute) {
  for (int i = 0; i < attrappe_wecker_zahl(); i++) {
    if (attrappe_wecker(i)->cookie == COOKIE_AUFSCHUB + minute) return true;
  }
  return false;
}
// Wie "spaeter" im Erinnerungsfenster: die Runde `minute` vom Tag `tag`,
// deren einziger offener Platz Platz 0 ist (so stehen die Plaene hier).
static void aufschieben(int minute, int32_t tag) {
  const Aufschub a = { .minute = minute, .tag = tag, .plaetze = 0x01 };
  remind_snooze(&a);
}
static const AttrappeWecker *neuplanen(void) {
  const AttrappeWecker *gefunden = NULL;
  for (int i = 0; i < attrappe_wecker_zahl(); i++) {
    if (attrappe_wecker(i)->cookie == COOKIE_NEUPLANEN) {
      if (gefunden) return NULL;   // zwei waeren einer zu viel
      gefunden = attrappe_wecker(i);
    }
  }
  return gefunden;
}

static void abschnitt_vorausplanen(void) {
  printf("\nVorausplanen (W-K1)\n");
  // 14.07.2026 (Dienstag), 10:00: die Runde von 08:00 ist vorbei.
  const int J = 2026, M = 7, T = 14;
  const int32_t heute = datum_tag(J, M, T);

  frisch(ortszeit(J, M, T, 10, 0));
  Platz zwei[] = { { "Zink", 8, 0, 2, 0, 0, heute } };
  plan_setzen(zwei, 1);
  remind_schedule();
  const AttrappeWecker *e = erste_erinnerung();
  pruefe("alle 2 Tage, um 10 Uhr gestellt: es steht eine Erinnerung", e != NULL);
  pruefe("die erste uebermorgen um 08:00", e && ist_um(e->zeit, J, M, T + 2, 8, 0) && e->cookie == 480);
  pruefe("verpasst meldet sie sich", e && e->melden);
  pruefe("sieben Erinnerungen, zwei Tage auseinander",
         erinnerungen() == 7 && ist_um(erinnerung(6)->zeit, J, M, T + 14, 8, 0));
  if (erinnerungen() != 7) zeige("alle 2 Tage");

  frisch(ortszeit(J, M, T, 10, 0));
  Platz drei[] = { { "Zink", 8, 0, 3, 0, 0, heute } };
  plan_setzen(drei, 1);
  remind_schedule();
  e = erste_erinnerung();
  pruefe("alle 3 Tage: erste in drei Tagen", e && ist_um(e->zeit, J, M, T + 3, 8, 0));

  // 1 Woche an, 1 aus, heute der letzte Einnahmetag (Anker vor 6 Tagen):
  // Pause an Tag 7..13 nach dem Anker, weiter an Tag 14 = heute + 8.
  frisch(ortszeit(J, M, T, 10, 0));
  Platz kur[] = { { "Maca", 8, 0, 1, 1, 1, heute - 6 } };
  plan_setzen(kur, 1);
  remind_schedule();
  e = erste_erinnerung();
  pruefe("1 an/1 aus am letzten Einnahmetag: die naechste nach der Pause, heute + 8",
         e && ist_um(e->zeit, J, M, T + 8, 8, 0));
  if (!e) zeige("1 an/1 aus");

  // Sechs taegliche zu verschiedenen Zeiten: sieben Erinnerungen und der
  // Wecker zum Neuplanen - mehr als acht erlaubt die Uhr nicht.
  frisch(ortszeit(J, M, T, 6, 0));
  Platz sechs[] = { { "A", 7, 0, 1, 0, 0, heute }, { "B", 9, 0, 1, 0, 0, heute }, { "C", 11, 0, 1, 0, 0, heute },
                    { "D", 13, 0, 1, 0, 0, heute }, { "E", 17, 0, 1, 0, 0, heute }, { "F", 21, 0, 1, 0, 0, heute } };
  plan_setzen(sechs, 6);
  remind_schedule();
  pruefe("sechs taegliche: 7 Wecker, alle Erinnerungen", attrappe_wecker_zahl() == 7 && erinnerungen() == 7);
  pruefe("die siebte ist morgen 07:00", erinnerung(6) && ist_um(erinnerung(6)->zeit, J, M, T + 1, 7, 0));
  pruefe("kein Wecker zum Neuplanen - die naechste Erinnerung plant neu", neuplanen() == NULL);

  // Heute schon genommen: heute keiner mehr fuer diese Runde, morgen schon.
  frisch(ortszeit(J, M, T, 7, 30));
  Platz eins[] = { { "Zink", 8, 0, 1, 0, 0, heute } };
  plan_setzen(eins, 1);
  plan_set_taken(0, true);
  remind_schedule();
  e = erste_erinnerung();
  pruefe("heute genommen: die erste ist morgen 08:00", e && ist_um(e->zeit, J, M, T + 1, 8, 0));
}

static void abschnitt_neuplanen(void) {
  printf("\nDer Wecker zum Neuplanen\n");
  const int J = 2026, M = 7, T = 14;
  const int32_t heute = datum_tag(J, M, T);

  // Ein Zyklus mitten in einer langen Pause: in 60 Tagen nichts faellig.
  // Bis 0.15.0 stand dann gar kein Wecker; danach nur der zum Neuplanen.
  // 1 Woche an, 52 aus, Anker vor 8 Tagen: wieder dran an heute + 363.
  frisch(ortszeit(J, M, T, 10, 0));
  Platz pause[] = { { "Maca", 8, 0, 1, 1, 52, heute - 8 } };
  plan_setzen(pause, 1);
  remind_schedule();
  const AttrappeWecker *n = neuplanen();
  pruefe("lange Pause: der Wecker zum Neuplanen steht", n != NULL);
  pruefe("er klopft morgen um 03:00", n && ist_um(n->zeit, J, M, T + 1, 3, 0));
  pruefe("verpasst meldet er sich nicht", n && !n->melden);
  const AttrappeWecker *e = erste_erinnerung();
  pruefe("und die erste Erinnerung nach der Pause steht: heute + 363, 08:00",
         erinnerungen() >= 1 && e && ist_um(e->zeit, J, M, T + 363, 8, 0));
  pruefe("  sie meldet sich, wenn die Uhr sie verpasst", e && e->melden);
  if (!e) zeige("lange Pause");
  // Die Uhr ist um 03:00 aus (Akku leer) und startet um 09:00: der stille
  // Wecker ist weg, wie in pebbleos wakeup_init. Die Kette steht trotzdem.
  attrappe_uhr_aus_bis(ortszeit(J, M, T + 1, 9, 0));
  pruefe("Uhr um 03:00 aus: der Wecker zum Neuplanen ist weg", neuplanen() == NULL);
  e = erste_erinnerung();
  pruefe("  die Erinnerung nach der Pause steht noch", e && ist_um(e->zeit, J, M, T + 363, 8, 0));

  // Nichts mehr faellig, auch nicht in SC_SUCHE_TAGE: eine beendete Kur.
  // Dann nuetzt auch der Wecker zum Neuplanen nichts - ein neuer Plan kommt
  // nur bei offener App -, er verdraengte nur jede Nacht, was um drei laeuft.
  frisch(ortszeit(J, M, T, 10, 0));
  Platz kur_vorbei[] = { { "Rhodiola", 8, 0, 1, 4, 0, heute - 100 } };
  plan_setzen(kur_vorbei, 1);
  remind_schedule();
  pruefe("beendete Kur: keine Erinnerung und kein Wecker zum Neuplanen", attrappe_wecker_zahl() == 0);
  pruefe("  und das steht im Log", strstr(attrappe_log_text, "keine Erinnerung faellig") != NULL &&
                                   strstr(attrappe_log_text, "nichts wird wieder faellig") != NULL);
  // Neben der beendeten eine alle 2 Tage: die steht weiter an, also auch
  // der Wecker zum Neuplanen.
  frisch(ortszeit(J, M, T, 10, 0));
  Platz gemischt[] = { { "Rhodiola", 8, 0, 1, 4, 0, heute - 100 }, { "Zink", 9, 0, 2, 0, 0, heute } };
  plan_setzen(gemischt, 2);
  remind_schedule();
  pruefe("beendete Kur und alle 2 Tage: der Wecker zum Neuplanen steht", neuplanen() != NULL);

  // Vor drei Uhr gestellt: noch heute.
  frisch(ortszeit(J, M, T, 2, 0));
  plan_setzen(pause, 1);
  remind_schedule();
  n = neuplanen();
  pruefe("um 02:00 gestellt: heute um 03:00", n && ist_um(n->zeit, J, M, T, 3, 0));

  // Ohne Plan kein Wecker: ein Plan kommt nur bei offener App.
  frisch(ortszeit(J, M, T, 10, 0));
  remind_schedule();
  pruefe("ohne Plan: gar kein Wecker", attrappe_wecker_zahl() == 0);

  // In der Nacht der Umstellung: 03:00 gibt es, und er liegt dort. Mitten
  // in einer langen Pause, sonst stuende er gar nicht.
  frisch(ortszeit(2026, 3, 29, 1, 0));
  Platz pause_maerz[] = { { "Maca", 8, 0, 1, 1, 52, datum_tag(2026, 3, 29) - 8 } };
  plan_setzen(pause_maerz, 1);
  remind_schedule();
  n = neuplanen();
  pruefe("29.03.2026 01:00 gestellt: 03:00 am selben Tag", n && ist_um(n->zeit, 2026, 3, 29, 3, 0));
  frisch(ortszeit(2026, 10, 24, 22, 0));
  Platz pause_okt[] = { { "Maca", 8, 0, 1, 1, 52, datum_tag(2026, 10, 24) - 8 } };
  plan_setzen(pause_okt, 1);
  remind_schedule();
  n = neuplanen();
  pruefe("24.10.2026 22:00 gestellt: 25.10. 03:00", n && ist_um(n->zeit, 2026, 10, 25, 3, 0));

  // Erinnerungen als Erinnerung erkennbar, Neuplanen nicht.
  pruefe("Cookie 480 ist die Runde 08:00", remind_cookie_minute(480) == 480 && !remind_cookie_neuplanen(480));
  pruefe("Aufschub der Runde 08:00 ist die Runde 08:00", remind_cookie_minute(COOKIE_AUFSCHUB + 480) == 480);
  pruefe("Neuplanen ist keine Runde", remind_cookie_minute(COOKIE_NEUPLANEN) == -1 &&
                                      remind_cookie_neuplanen(COOKIE_NEUPLANEN));
  pruefe("Unsinn ist keine Runde", remind_cookie_minute(1440) == -1 && remind_cookie_minute(-1) == -1);
}

// Der Wecker zum Neuplanen startet die App im Vordergrund - um 03:00
// verdraengt er, was gerade laeuft. Er steht nur, wenn bis zum 03:00 nach
// dem naechsten keine Erinnerung kommt.
static void abschnitt_neuplanen_selten(void) {
  printf("\nDer Wecker zum Neuplanen nur, wenn sonst lange nichts klopft\n");
  const int J = 2026, M = 7, T = 14;
  const int32_t heute = datum_tag(J, M, T);
  Platz taeglich[] = { { "Zink", 8, 0, 1, 0, 0, heute }, { "Maca", 12, 30, 1, 0, 0, heute } };

  frisch(ortszeit(J, M, T, 7, 0));
  plan_setzen(taeglich, 2);
  remind_schedule();
  pruefe("taeglich, um 07:00 gestellt: kein Wecker zum Neuplanen", neuplanen() == NULL && erinnerungen() == 7);
  // Der Fall, um den es geht: nach der letzten Runde des Abends liegt die
  // naechste (morgen 08:00) hinter dem naechsten 03:00.
  frisch(ortszeit(J, M, T, 21, 0));
  plan_setzen(taeglich, 2);
  remind_schedule();
  pruefe("taeglich, am Abend gestellt: auch nachts keiner", neuplanen() == NULL);
  pruefe("  die naechste Erinnerung ist morgen 08:00", erste_erinnerung() && ist_um(erste_erinnerung()->zeit, J, M, T + 1, 8, 0));
  if (neuplanen()) zeige("taeglich am Abend");

  // Alle 2 Tage, heute dran und vorbei: uebermorgen 08:00 liegt hinter dem
  // 03:00 nach dem naechsten - also morgen um drei neu planen.
  Platz zwei[] = { { "Zink", 8, 0, 2, 0, 0, heute } };
  frisch(ortszeit(J, M, T, 10, 0));
  plan_setzen(zwei, 1);
  remind_schedule();
  const AttrappeWecker *n = neuplanen();
  pruefe("alle 2 Tage, am Einnahmetag gestellt: Neuplanen morgen 03:00", n && ist_um(n->zeit, J, M, T + 1, 3, 0));
  // Er klopft und plant neu: jetzt kommt die Erinnerung vor dem 03:00 danach.
  stub_jetzt = ortszeit(J, M, T + 1, 3, 0);
  remind_schedule();
  pruefe("  um 03:00 neu geplant: in dieser Nacht keiner mehr", neuplanen() == NULL);
  pruefe("  die Erinnerung bleibt: morgen 08:00", erste_erinnerung() && ist_um(erste_erinnerung()->zeit, J, M, T + 2, 8, 0));

  // Ein Aufschub laeuft: er oeffnet die App ohnehin in 15 min.
  frisch(ortszeit(J, M, T, 8, 0));
  Platz pause[] = { { "Maca", 8, 0, 1, 1, 52, heute - 6 } };   // heute der letzte Einnahmetag
  plan_setzen(pause, 1);
  remind_geklopft(480);          // der Wecker 08:00 hat die App gestartet
  aufschieben(480, heute);
  pruefe("Pause ab morgen, aber ein Aufschub laeuft: kein Wecker zum Neuplanen",
         aufschub_wecker_um(480) && neuplanen() == NULL);
  remind_snooze_clear(480);
  remind_schedule();
  pruefe("  Aufschub vorbei: er steht wieder", neuplanen() != NULL);
}

// Taeglich 08:00, gestellt am Vortag 10:00: am Tag der Umstellung um 08:00.
static void umstellung(int j, int mo, int t) {
  frisch(ortszeit(j, mo, t - 1, 10, 0));
  Platz eins[] = { { "Zink", 8, 0, 1, 0, 0, datum_tag(j, mo, t - 1) } };
  plan_setzen(eins, 1);
  remind_schedule();
  const AttrappeWecker *e = erste_erinnerung();
  char was[96];
  snprintf(was, sizeof(was), "%04d-%02d-%02d: taeglich 08:00 klopft um 08:00", j, mo, t);
  pruefe(was, e && ist_um(e->zeit, j, mo, t, 8, 0));
  if (!(e && ist_um(e->zeit, j, mo, t, 8, 0))) zeige(was);
  snprintf(was, sizeof(was), "%04d-%02d-%02d: und am Tag danach auch", j, mo, t);
  pruefe(was, erinnerung(1) && ist_um(erinnerung(1)->zeit, j, mo, t + 1, 8, 0));
  // Um 00:30 am Umstellungstag gestellt: die Zeiten dieses Tages stimmen.
  frisch(ortszeit(j, mo, t, 0, 30));
  plan_setzen(eins, 1);
  remind_schedule();
  e = erste_erinnerung();
  snprintf(was, sizeof(was), "%04d-%02d-%02d 00:30 gestellt: 08:00 am selben Tag", j, mo, t);
  pruefe(was, e && ist_um(e->zeit, j, mo, t, 8, 0));
}

static void abschnitt_umstellung(void) {
  printf("\nUhrzeit am Umstellungstag (M2)\n");
  umstellung(2026, 3, 29);
  umstellung(2026, 10, 25);
  umstellung(2027, 3, 28);
  umstellung(2026, 3, 8);     // USA
  umstellung(2026, 11, 1);    // USA
  // Eine Uhrzeit, die es nicht gibt, faellt nicht aus: sie kommt spaeter.
  const time_t luecke = kalender_zeit_am(datum_tag(2026, 3, 29), 2 * 60 + 30);
  const struct tm l = *localtime(&luecke);
  pruefe("02:30 am 29.03.2026 liegt am selben Tag, nicht frueher als 02:30",
         l.tm_mday == 29 && l.tm_hour * 60 + l.tm_min >= 2 * 60 + 30);
  // Eine, die es zweimal gibt, kommt einmal und richtig.
  const time_t doppelt = kalender_zeit_am(datum_tag(2026, 10, 25), 2 * 60 + 30);
  const struct tm d = *localtime(&doppelt);
  pruefe("02:30 am 25.10.2026 ist 02:30", d.tm_mday == 25 && d.tm_hour == 2 && d.tm_min == 30);
}

static void abschnitt_aufschub(void) {
  printf("\nDer Aufschub bleibt vorn\n");
  const int32_t heute = datum_tag(2026, 7, 14);
  frisch(ortszeit(2026, 7, 14, 8, 2));
  Platz sechs[] = { { "A", 8, 0, 1, 0, 0, heute }, { "B", 9, 0, 1, 0, 0, heute }, { "C", 11, 0, 1, 0, 0, heute },
                    { "D", 13, 0, 1, 0, 0, heute }, { "E", 17, 0, 1, 0, 0, heute }, { "F", 21, 0, 1, 0, 0, heute } };
  plan_setzen(sechs, 6);
  aufschieben(480, heute);
  bool aufschub = false;
  for (int i = 0; i < attrappe_wecker_zahl(); i++) {
    if (attrappe_wecker(i)->cookie == COOKIE_AUFSCHUB + 480 && ist_um(attrappe_wecker(i)->zeit, 2026, 7, 14, 8, 17)) aufschub = true;
  }
  pruefe("Aufschub um 08:17 steht", aufschub);
  pruefe("dazu sechs Erinnerungen, kein Wecker zum Neuplanen", neuplanen() == NULL && attrappe_wecker_zahl() == 7);
  remind_snooze_clear(480);
}

// Der Aufschub-Wecker dieser Runde, oder NULL.
static const AttrappeWecker *aufschub_wecker(int minute) {
  for (int i = 0; i < attrappe_wecker_zahl(); i++) {
    if (attrappe_wecker(i)->cookie == COOKIE_AUFSCHUB + minute) return attrappe_wecker(i);
  }
  return NULL;
}

static void abschnitt_aufschub_zaehlt(void) {
  printf("\nHoechstens drei Aufschuebe, auch wenn der Aufschub klopft\n");
  const int32_t heute = datum_tag(2026, 7, 14);
  frisch(ortszeit(2026, 7, 14, 8, 0));
  Platz zwei[] = { { "A", 8, 0, 1, 0, 0, heute }, { "B", 12, 30, 1, 0, 0, heute } };
  plan_setzen(zwei, 2);
  // Wie auf der Uhr: "spaeter", der Aufschub klopft, die App startet und
  // stellt alle Wecker neu - dreimal hintereinander.
  for (int k = 1; k <= SC_SNOOZE_MAX; k++) {
    char was[96];
    // Das erste Fenster kam vom Wecker der Runde, die weiteren vom Aufschub.
    const int32_t cookie = k == 1 ? 480 : COOKIE_AUFSCHUB + 480;
    snprintf(was, sizeof(was), "vor dem %d. Aufschub ist einer uebrig", k);
    pruefe(was, remind_snooze_left(480, remind_runden_tag(cookie)));
    aufschieben(480, remind_runden_tag(cookie));   // wie das Erinnerungsfenster
    const AttrappeWecker *a = aufschub_wecker(480);
    snprintf(was, sizeof(was), "der %d. steht 15 min spaeter", k);
    pruefe(was, a && a->zeit == stub_jetzt + SC_SNOOZE_MIN * 60);
    if (!a) return;
    stub_jetzt = a->zeit;         // er klopft ...
    remind_geklopft(COOKIE_AUFSCHUB + 480);
    remind_schedule();            // ... und die App stellt neu
    snprintf(was, sizeof(was), "nach dem Klopfen zaehlt er %d", k);
    pruefe(was, remind_snooze_count(480, heute) == k);
    pruefe("  und klopft nicht nochmal", aufschub_wecker(480) == NULL);
  }
  pruefe("nach dem dritten ist keiner mehr uebrig", !remind_snooze_left(480, heute));
  pruefe("die Runde 12:30 hat ihre eigenen drei", remind_snooze_count(750, heute) == 0 && remind_snooze_left(750, heute));

  // Morgen ist die Runde 08:00 ein neuer Anlass.
  stub_jetzt = ortszeit(2026, 7, 15, 8, 0);
  remind_geklopft(480);
  remind_schedule();
  pruefe("morgen faengt die Runde 08:00 bei null an",
         remind_runden_tag(480) == heute + 1 && remind_snooze_count(480, heute + 1) == 0 &&
         remind_snooze_left(480, heute + 1));
  aufschieben(480, remind_runden_tag(480));
  pruefe("  und zaehlt dann 1", remind_snooze_count(480, heute + 1) == 1);
  // Wie das Erinnerungsfenster nach Abhaken und Wegdruecken: vergessen,
  // dann neu stellen.
  remind_snooze_clear(750);
  remind_schedule();
  pruefe("Abhaken und Wegdruecken einer anderen Runde lassen ihn stehen",
         remind_snooze_count(480, heute + 1) == 1 && aufschub_wecker(480) != NULL);
  remind_snooze_clear(480);
  remind_schedule();
  pruefe("die eigene Runde loescht ihn", remind_snooze_count(480, heute + 1) == 0 && aufschub_wecker(480) == NULL);

  // Ein Aufschub, den 0.15.0 hinterliess (ohne Tag): er klopft noch, zaehlt
  // aber nicht - lieber einmal zu oft aufschieben als zu frueh verfallen.
  frisch(ortszeit(2026, 7, 14, 8, 5));
  plan_setzen(zwei, 2);
  persist_write_int(FACH_AUFSCHUB_ZEIT, (int)ortszeit(2026, 7, 14, 8, 15));
  persist_write_int(FACH_AUFSCHUB_MINUTE, 480);
  persist_write_int(FACH_AUFSCHUB_ZAHL, 3);
  remind_schedule();
  pruefe("0.15.0-Aufschub ohne Tag: er klopft um 08:15", aufschub_wecker(480) && ist_um(aufschub_wecker(480)->zeit, 2026, 7, 14, 8, 15));
  pruefe("  und laesst noch Aufschuebe uebrig", remind_snooze_left(480, remind_runden_tag(COOKIE_AUFSCHUB + 480)));
  remind_snooze_clear(480);
}

// Gibt es einen Wecker mit diesem Cookie um diese Ortszeit?
static bool wecker_um(int32_t cookie, int j, int mo, int t, int h, int mi) {
  for (int i = 0; i < attrappe_wecker_zahl(); i++) {
    if (attrappe_wecker(i)->cookie == cookie && ist_um(attrappe_wecker(i)->zeit, j, mo, t, h, mi)) return true;
  }
  return false;
}

static void abschnitt_kurz_davor(void) {
  printf("\nKurz vor dem Wecker neu gestellt\n");
  const int J = 2026, M = 7, T = 14;
  const int32_t heute = datum_tag(J, M, T);
  Platz zwei[] = { { "A", 8, 0, 1, 0, 0, heute }, { "B", 12, 30, 1, 0, 0, heute } };

  // "Spaeter" um 08:00, dann die App von Hand geoeffnet, kurz bevor der
  // Aufschub klopft: der Start stellt alle Wecker neu.
  const int sekunden_davor[] = { 15, 30, 1 };
  for (unsigned k = 0; k < sizeof(sekunden_davor) / sizeof(sekunden_davor[0]); k++) {
    frisch(ortszeit(J, M, T, 8, 0));
    plan_setzen(zwei, 2);
    aufschieben(480, heute);
    const time_t soll = stub_jetzt + SC_SNOOZE_MIN * 60;
    stub_jetzt = soll - sekunden_davor[k];
    remind_schedule();
    const AttrappeWecker *a = aufschub_wecker(480);
    char was[96];
    snprintf(was, sizeof(was), "%d s vor dem Aufschub neu gestellt: er steht noch", sekunden_davor[k]);
    pruefe(was, a != NULL);
    // Bis sc-r2 schob ihn jedes Neustellen in den 30 s davor auf "jetzt +
    // 31 s". Die Uhr nimmt jeden Wecker, der in der Zukunft liegt.
    pruefe("  zu seiner Zeit, hoechstens 3 s spaeter",
           a && a->zeit >= soll && a->zeit <= soll + 3);
    pruefe("  und zaehlt weiter als derselbe", remind_snooze_count(480, heute) == 1);
  }
  // Klopft er gerade (Start durch ihn), steht er nicht nochmal.
  frisch(ortszeit(J, M, T, 8, 0));
  plan_setzen(zwei, 2);
  aufschieben(480, heute);
  stub_jetzt += SC_SNOOZE_MIN * 60;
  remind_geklopft(COOKIE_AUFSCHUB + 480);
  remind_schedule();
  pruefe("beim Klopfen selbst: kein zweiter", aufschub_wecker(480) == NULL);

  // Dasselbe fuer eine gewoehnliche Runde: um 07:59:45 geoeffnet.
  frisch(ortszeit(J, M, T, 7, 59) + 45);
  plan_setzen(zwei, 2);
  remind_schedule();
  pruefe("um 07:59:45 neu gestellt: die Runde 08:00 klopft heute noch", wecker_um(480, J, M, T, 8, 0));
  stub_jetzt = ortszeit(J, M, T, 8, 0);
  remind_geklopft(480);
  remind_schedule();
  pruefe("um 08:00:00 (sie klopft gerade) nicht nochmal heute", !wecker_um(480, J, M, T, 8, 0) &&
         wecker_um(480, J, M, T + 1, 8, 0));
}

// Der erste Wecker mit diesem Cookie, oder NULL.
static const AttrappeWecker *wecker_mit(int32_t cookie) {
  for (int i = 0; i < attrappe_wecker_zahl(); i++) {
    if (attrappe_wecker(i)->cookie == cookie) return attrappe_wecker(i);
  }
  return NULL;
}

// Neu gestellt in den Sekunden um einen Wecker, bevor er klopfte: er bleibt
// und wandert jedes Mal hoechstens ein paar Sekunden. Bis sc-r2 schloss
// remind_schedule aus "Zeit vorbei" auf "hat geklopft": um 08:14:45
// geoeffnet stand der Aufschub auf 08:15:16, ein Haken um 08:15:05 - und er
// war weg. Ebenso eine Runde.
static void abschnitt_dazwischen(void) {
  printf("\nNeu gestellt, bevor der Wecker klopfte\n");
  const int J = 2026, M = 7, T = 14;
  const int32_t heute = datum_tag(J, M, T);
  Platz zwei[] = { { "A", 8, 0, 1, 0, 0, heute }, { "B", 12, 30, 1, 0, 0, heute } };
  char was[96];

  frisch(ortszeit(J, M, T, 8, 0));
  plan_setzen(zwei, 2);
  aufschieben(480, heute);
  const time_t soll = stub_jetzt + SC_SNOOZE_MIN * 60;
  const int um[] = { -15, -1, 1, 2, 4, 30 };   // Sekunden zu seiner Zeit
  const AttrappeWecker *a = NULL;
  for (unsigned k = 0; k < sizeof(um) / sizeof(um[0]); k++) {
    stub_jetzt = soll + um[k];
    remind_schedule();
    a = aufschub_wecker(480);
    snprintf(was, sizeof(was), "Aufschub, %+d s zu seiner Zeit neu gestellt: er steht, frueh genug", um[k]);
    pruefe(was, a && a->zeit >= soll && a->zeit > stub_jetzt && a->zeit <= (soll > stub_jetzt ? soll : stub_jetzt) + 3);
  }
  if (!a) return;
  stub_jetzt = a->zeit;
  remind_geklopft(a->cookie);
  remind_schedule();
  pruefe("  geklopft: kein Aufschub-Wecker mehr, der Zaehler bleibt",
         aufschub_wecker(480) == NULL && remind_snooze_count(480, heute) == 1);
  // Die Uhr war aus: eine Minute nach seiner Zeit gilt er als verpasst.
  frisch(ortszeit(J, M, T, 8, 0));
  plan_setzen(zwei, 2);
  aufschieben(480, heute);
  stub_jetzt = soll + 60;
  remind_schedule();
  pruefe("Aufschub 60 s nach seiner Zeit, nie geklopft: verpasst",
         aufschub_wecker(480) == NULL && !persist_exists(FACH_AUFSCHUB_ZEIT) && remind_snooze_count(480, heute) == 1);
  pruefe("  das steht im Log", strstr(attrappe_log_text, "Aufschub der Minute 480 verpasst") != NULL);

  // Eine Runde. Gestern hat 08:00 geklopft, heute wird um 07:59:59 und um
  // 08:00:01 neu gestellt (zwei Haken auf dem Heute-Schirm).
  frisch(ortszeit(J, M, T - 1, 8, 0));
  plan_setzen(zwei, 2);
  remind_geklopft(480);
  const time_t acht = ortszeit(J, M, T, 8, 0);
  const int runde_um[] = { -1, 1, 3 };
  const AttrappeWecker *r = NULL;
  for (unsigned k = 0; k < sizeof(runde_um) / sizeof(runde_um[0]); k++) {
    stub_jetzt = acht + runde_um[k];
    remind_schedule();
    r = wecker_mit(480);
    snprintf(was, sizeof(was), "Runde 08:00, %+d s neu gestellt: sie steht heute, frueh genug", runde_um[k]);
    pruefe(was, r && r->zeit >= acht && r->zeit > stub_jetzt && r->zeit <= (acht > stub_jetzt ? acht : stub_jetzt) + 3);
  }
  if (!r) return;
  stub_jetzt = r->zeit;
  remind_geklopft(480);
  remind_schedule();
  r = wecker_mit(480);
  pruefe("  geklopft: heute nicht nochmal, morgen 08:00", r && ist_um(r->zeit, J, M, T + 1, 8, 0));
  // Laenger als eine Minute vorbei und nie geklopft (Uhr aus): nicht nachholen.
  frisch(ortszeit(J, M, T - 1, 8, 0));
  plan_setzen(zwei, 2);
  remind_geklopft(480);
  stub_jetzt = acht + 60;
  remind_schedule();
  r = wecker_mit(480);
  pruefe("Runde 08:00, 60 s vorbei, nie geklopft: nicht nachgeholt", r && ist_um(r->zeit, J, M, T + 1, 8, 0));
  // Nach einer Reise nach Westen klopft 08:00 schon um 02:00. Die Runde
  // 05:00 dazwischen hat damit nicht geklopft: um 04:59:59 und 05:00:01 neu
  // gestellt, steht sie noch.
  frisch(ortszeit(J, M, T, 2, 0));
  Platz frueh[] = { { "A", 8, 0, 1, 0, 0, heute }, { "C", 5, 0, 1, 0, 0, heute } };
  plan_setzen(frueh, 2);
  remind_geklopft(480);
  stub_jetzt = ortszeit(J, M, T, 5, 0) - 1;
  remind_schedule();
  stub_jetzt = ortszeit(J, M, T, 5, 0) + 1;
  remind_schedule();
  r = wecker_mit(300);
  pruefe("08:00 klopfte um 02:00: die Runde 05:00 gilt nicht als geklopft",
         r && r->zeit > stub_jetzt && r->zeit <= stub_jetzt + 3);
}

// Runde 23:50 am Datum j-mo-t, aufgeschoben ueber Mitternacht.
static void mitternacht(int J, int M, int T) {
  const int32_t heute = datum_tag(J, M, T);
  char was[128];
  frisch(ortszeit(J, M, T, 23, 50));
  Platz spaet[] = { { "Mg", 23, 50, 1, 0, 0, heute - 10 } };
  plan_setzen(spaet, 1);
  aufschieben(1430, remind_runden_tag(1430));   // vom Wecker der Runde
  const AttrappeWecker *a = aufschub_wecker(1430);
  snprintf(was, sizeof(was), "%04d-%02d-%02d 23:50 aufgeschoben: er klopft um 00:05", J, M, T);
  pruefe(was, a && ist_um(a->zeit, J, M, T + 1, 0, 5));
  if (!a) { zeige(was); return; }
  for (int k = 1; k <= SC_SNOOZE_MAX; k++) {
    stub_jetzt = a->zeit;         // er klopft ...
    remind_geklopft(COOKIE_AUFSCHUB + 1430);
    remind_schedule();            // ... und die App stellt neu
    snprintf(was, sizeof(was), "  nach Mitternacht die Runde von gestern, Aufschub %d", k);
    pruefe(was, remind_runden_tag(COOKIE_AUFSCHUB + 1430) == heute && remind_snooze_count(1430, heute) == k);
    if (k == SC_SNOOZE_MAX) break;
    aufschieben(1430, remind_runden_tag(COOKIE_AUFSCHUB + 1430));
    a = aufschub_wecker(1430);
    pruefe("  und klopft 15 min spaeter wieder", a && a->zeit == stub_jetzt + SC_SNOOZE_MIN * 60);
    if (!a) return;
  }
  pruefe("  nach dem dritten ist keiner mehr uebrig", !remind_snooze_left(1430, heute));
  pruefe("  die Runde 23:50 von heute klopft trotzdem", wecker_um(1430, J, M, T + 1, 23, 50));
  stub_jetzt = ortszeit(J, M, T + 1, 23, 50);
  pruefe("  um 23:50 ist es die Runde von heute, bei null",
         remind_runden_tag(1430) == heute + 1 && remind_snooze_count(1430, heute + 1) == 0);
}

static void abschnitt_mitternacht(void) {
  printf("\nAufschub ueber Mitternacht\n");
  mitternacht(2026, 7, 14);
  mitternacht(2026, 3, 28);     // in die Nacht der Umstellung (Europa)
  mitternacht(2026, 10, 24);
  mitternacht(2026, 3, 7);      // USA
  mitternacht(2026, 10, 31);

  const int J = 2026, M = 7, T = 14;
  const int32_t heute = datum_tag(J, M, T);
  // Alle 2 Tage: gestern dran, heute nicht. Der Aufschub gilt gestern.
  frisch(ortszeit(J, M, T, 23, 50));
  Platz zweitage[] = { { "Mg", 23, 50, 2, 0, 0, heute } };
  plan_setzen(zweitage, 1);
  aufschieben(1430, remind_runden_tag(1430));
  stub_jetzt = ortszeit(J, M, T + 1, 0, 1);
  remind_schedule();
  pruefe("alle 2 Tage, heute nicht dran: der Aufschub von gestern klopft", wecker_um(COOKIE_AUFSCHUB + 1430, J, M, T + 1, 0, 5));

  // Vor Mitternacht auf dem Heute-Schirm abgehakt: nach Mitternacht weiss
  // die Uhr das nicht mehr - der Aufschub muss schon vorher vorbei sein.
  frisch(ortszeit(J, M, T, 23, 50));
  Platz spaet[] = { { "Mg", 23, 50, 1, 0, 0, heute - 10 } };
  plan_setzen(spaet, 1);
  aufschieben(1430, remind_runden_tag(1430));
  stub_jetzt = ortszeit(J, M, T, 23, 55);
  plan_set_taken(0, true);
  remind_schedule();              // wie der Heute-Schirm nach dem Abhaken
  stub_jetzt = ortszeit(J, M, T + 1, 0, 1);
  remind_schedule();              // App um 00:01 geoeffnet
  pruefe("vor Mitternacht abgehakt: danach klopft er nicht", aufschub_wecker(1430) == NULL);

  // Ein Aufschub derselben Uhrzeit, aber vom Tag vor gestern (Uhr verstellt):
  // nie.
  frisch(ortszeit(J, M, T, 23, 50));
  plan_setzen(spaet, 1);
  aufschieben(1430, heute - 1);
  stub_jetzt = ortszeit(J, M, T, 23, 51);
  persist_write_int(FACH_AUFSCHUB_ZEIT, (int)ortszeit(J, M, T + 1, 0, 5));
  remind_schedule();
  pruefe("ein Aufschub, der zwei Tage nach seiner Runde klopfte: nie", aufschub_wecker(1430) == NULL);
}

// Ein Aufschub, dessen Runde heute erledigt ist, ist ganz vorbei - nicht nur
// sein Wecker. Und nur ein Aufschub-Wecker kann der Runde von gestern gelten.
static void abschnitt_erledigt(void) {
  printf("\nErledigt: der ganze Aufschub ist vorbei; gestern nur fuer den Aufschub\n");
  const int J = 2026, M = 7, T = 14;
  const int32_t heute = datum_tag(J, M, T);
  Platz zwei[] = { { "Zink", 8, 0, 1, 0, 0, heute }, { "Maca", 12, 30, 1, 0, 0, heute } };
  frisch(ortszeit(J, M, T, 8, 0));
  plan_setzen(zwei, 2);
  aufschieben(480, heute);
  stub_jetzt = ortszeit(J, M, T, 8, 10);
  plan_set_taken(0, true);
  remind_schedule();              // wie der Heute-Schirm nach dem Abhaken
  pruefe("08:10 auf dem Heute-Schirm abgehakt: kein Aufschub-Wecker", aufschub_wecker(480) == NULL);
  pruefe("  und Minute, Zaehler und Tag sind auch weg",
         !persist_exists(FACH_AUFSCHUB_ZEIT) && !persist_exists(FACH_AUFSCHUB_MINUTE) &&
         !persist_exists(FACH_AUFSCHUB_ZAHL) && !persist_exists(FACH_AUFSCHUB_TAG));
  pruefe("  das steht im Log", strstr(attrappe_log_text, "Runde 480 erledigt") != NULL);

  // Liegen geblieben und offen: aufgeschoben, geklopft, nicht beantwortet.
  frisch(ortszeit(J, M, T, 8, 0));
  plan_setzen(zwei, 2);
  aufschieben(480, heute);
  stub_jetzt = ortszeit(J, M, T, 8, 15);
  remind_schedule();
  pruefe("unbeantwortet: Minute und Tag bleiben", persist_exists(FACH_AUFSCHUB_MINUTE) && persist_exists(FACH_AUFSCHUB_TAG));
  // Am naechsten Tag klopft der Wecker der Runde 08:00 vor ihrer Uhrzeit
  // (nach einer Reise nach Westen): es ist trotzdem die Runde von heute.
  stub_jetzt = ortszeit(J, M, T + 1, 2, 0);
  pruefe("ein gewoehnlicher Wecker um 02:00: die Runde von heute, bei null",
         remind_runden_tag(480) == heute + 1 && remind_snooze_count(480, remind_runden_tag(480)) == 0);
  pruefe("ein Aufschub-Wecker derselben Runde: die von gestern",
         remind_runden_tag(COOKIE_AUFSCHUB + 480) == heute);
  pruefe("ein Aufschub-Wecker einer anderen Runde: heute",
         remind_runden_tag(COOKIE_AUFSCHUB + 750) == heute + 1);
}

// Ein Aufschub mit Plaetzen von heute und vom Vortag (Fenster ueber
// Mitternacht zusammengefuehrt): erledigt ist er erst, wenn beides erledigt
// ist - den Vortag kann man nicht mehr abhaken, er bleibt offen.
static void abschnitt_vortag(void) {
  printf("\nAufschub mit Plaetzen vom Vortag\n");
  const int J = 2026, M = 7, T = 14;
  const int32_t heute = datum_tag(J, M, T);
  Platz zwei[] = { { "Mg", 23, 50, 1, 0, 0, heute - 10 }, { "Ca", 0, 10, 1, 0, 0, heute - 10 } };
  frisch(ortszeit(J, M, T + 1, 0, 10));
  plan_setzen(zwei, 2);
  const Aufschub a = { .minute = 10, .tag = heute + 1, .plaetze = 0x02, .plaetze_vortag = 0x01 };
  remind_snooze(&a);
  pruefe("00:10 aufgeschoben: er klopft um 00:25", wecker_um(COOKIE_AUFSCHUB + 10, J, M, T + 1, 0, 25));
  stub_jetzt = ortszeit(J, M, T + 1, 0, 15);
  plan_set_taken(1, true);        // Ca auf dem Heute-Schirm
  remind_schedule();
  pruefe("Ca abgehakt, Mg von gestern offen: er bleibt", wecker_um(COOKIE_AUFSCHUB + 10, J, M, T + 1, 0, 25));
  Aufschub b;
  pruefe("  mit Mg vom Vortag und ohne Ca", remind_aufschub(&b) && b.plaetze == 0 && b.plaetze_vortag == 0x01);
}

// Runden, die klopften, waehrend die Erinnerung auf das Telefon wartete, und
// mit der App verschwanden: in einer Minute wieder, ohne zu zaehlen - und
// ohne einen wartenden Aufschub zu verdraengen.
static void abschnitt_nachholen(void) {
  printf("\nNachholen, was mit der App verschwand\n");
  const int J = 2026, M = 7, T = 14;
  const int32_t heute = datum_tag(J, M, T);
  Platz zwei[] = { { "A", 8, 0, 1, 0, 0, heute }, { "B", 8, 1, 1, 0, 0, heute } };
  const Aufschub b = { .minute = 481, .tag = heute, .plaetze = 0x02 };
  frisch(ortszeit(J, M, T, 8, 1));
  plan_setzen(zwei, 2);
  remind_nachholen(&b);
  pruefe("B 08:01: um 08:02 wieder, Zaehler 0",
         wecker_um(COOKIE_AUFSCHUB + 481, J, M, T, 8, 2) && remind_snooze_count(481, heute) == 0);
  // Der Aufschub von 08:00 wartet (08:15): B kommt zu ihm.
  frisch(ortszeit(J, M, T, 8, 0));
  plan_setzen(zwei, 2);
  aufschieben(480, heute);
  stub_jetzt = ortszeit(J, M, T, 8, 1);
  remind_nachholen(&b);
  Aufschub g;
  pruefe("ein wartender Aufschub nimmt B auf und klopft um 08:02",
         wecker_um(COOKIE_AUFSCHUB + 480, J, M, T, 8, 2) && aufschub_wecker(481) == NULL);
  pruefe("  er bleibt der von 08:00, mit seinem Zaehler und A und B",
         remind_aufschub(&g) && g.minute == 480 && g.plaetze == 0x03 && remind_snooze_count(480, heute) == 1);
  // Einer von gestern wartet: er bleibt, B steht nur auf dem Heute-Schirm.
  frisch(ortszeit(J, M, T, 0, 1));
  plan_setzen(zwei, 2);
  aufschieben(480, heute - 1);
  stub_jetzt = ortszeit(J, M, T, 8, 1);
  persist_write_int(FACH_AUFSCHUB_ZEIT, (int)ortszeit(J, M, T, 8, 10));
  remind_nachholen(&b);
  pruefe("ein Aufschub von gestern wartet: er bleibt, B nicht", remind_aufschub(&g) && g.tag == heute - 1 &&
         strstr(attrappe_log_text, "bleibt auf dem Heute-Schirm") != NULL);
}

int main(void) {
  printf("Zeitzone: %s\n", getenv("TZ") ? getenv("TZ") : "(Rechner)");
  abschnitt_vorausplanen();
  abschnitt_neuplanen();
  abschnitt_neuplanen_selten();
  abschnitt_umstellung();
  abschnitt_aufschub();
  abschnitt_aufschub_zaehlt();
  abschnitt_kurz_davor();
  abschnitt_dazwischen();
  abschnitt_mitternacht();
  abschnitt_erledigt();
  abschnitt_vortag();
  abschnitt_nachholen();
  printf("%s\n", s_fehler ? "NICHT BESTANDEN" : "alles bestanden");
  return s_fehler ? 1 : 0;
}
