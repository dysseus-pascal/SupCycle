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
//   - DER WECKER ZUM NEUPLANEN steht immer, jede Nacht um drei, und meldet
//     sich nicht, wenn er verpasst wird.
//   - DIE UHRZEIT GILT AM UMSTELLUNGSTAG (Audit M2): 08:00 am Sonntag der
//     Umstellung ist 08:00 auf der Uhr, nicht 07:00 oder 09:00.
//   - HOECHSTENS DREI AUFSCHUEBE. Bis 0.15.0 loeschte das Neustellen nach
//     dem Klopfen des Aufschubs den Zaehler mit - jeder war wieder der erste.
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
// Frischer Start um `jetzt`: leerer Persist, keine Wecker.
static void frisch(time_t jetzt) {
  attrappe_persist_leeren();
  attrappe_wecker_leeren();
  stub_jetzt = jetzt;
  plan_init();
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
  pruefe("sechs taegliche: 8 Wecker, davon 7 Erinnerungen", attrappe_wecker_zahl() == 8 && erinnerungen() == 7);
  pruefe("die siebte ist morgen 07:00", erinnerung(6) && ist_um(erinnerung(6)->zeit, J, M, T + 1, 7, 0));
  pruefe("und der Wecker zum Neuplanen steht", neuplanen() != NULL);

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
  // Bis 0.15.0 stand dann gar kein Wecker - jetzt der zum Neuplanen.
  frisch(ortszeit(J, M, T, 10, 0));
  Platz pause[] = { { "Maca", 8, 0, 1, 1, 52, heute - 8 } };
  plan_setzen(pause, 1);
  remind_schedule();
  const AttrappeWecker *n = neuplanen();
  pruefe("lange Pause: keine Erinnerung, aber der Wecker zum Neuplanen", erinnerungen() == 0 && n != NULL);
  pruefe("er klopft morgen um 03:00", n && ist_um(n->zeit, J, M, T + 1, 3, 0));
  pruefe("verpasst meldet er sich nicht", n && !n->melden);

  // Vor drei Uhr gestellt: noch heute.
  frisch(ortszeit(J, M, T, 2, 0));
  plan_setzen(pause, 1);
  remind_schedule();
  n = neuplanen();
  pruefe("um 02:00 gestellt: heute um 03:00", n && ist_um(n->zeit, J, M, T, 3, 0));

  // Ohne Plan steht er auch - die App plant beim naechsten Mal selbst.
  frisch(ortszeit(J, M, T, 10, 0));
  remind_schedule();
  pruefe("ohne Plan: nur der Wecker zum Neuplanen", attrappe_wecker_zahl() == 1 && neuplanen() != NULL);

  // In der Nacht der Umstellung: 03:00 gibt es, und er liegt dort.
  frisch(ortszeit(2026, 3, 29, 1, 0));
  remind_schedule();
  n = neuplanen();
  pruefe("29.03.2026 01:00 gestellt: 03:00 am selben Tag", n && ist_um(n->zeit, 2026, 3, 29, 3, 0));
  frisch(ortszeit(2026, 10, 24, 22, 0));
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
  remind_snooze(480);
  bool aufschub = false;
  for (int i = 0; i < attrappe_wecker_zahl(); i++) {
    if (attrappe_wecker(i)->cookie == COOKIE_AUFSCHUB + 480 && ist_um(attrappe_wecker(i)->zeit, 2026, 7, 14, 8, 17)) aufschub = true;
  }
  pruefe("Aufschub um 08:17 steht", aufschub);
  pruefe("dazu der Wecker zum Neuplanen, zusammen 8", neuplanen() != NULL && attrappe_wecker_zahl() == 8);
  remind_snooze_clear();
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
    snprintf(was, sizeof(was), "vor dem %d. Aufschub ist einer uebrig", k);
    pruefe(was, remind_snooze_left(480));
    remind_snooze(480);
    const AttrappeWecker *a = aufschub_wecker(480);
    snprintf(was, sizeof(was), "der %d. steht 15 min spaeter", k);
    pruefe(was, a && a->zeit == stub_jetzt + SC_SNOOZE_MIN * 60);
    if (!a) return;
    stub_jetzt = a->zeit;         // er klopft ...
    remind_schedule();            // ... und die App stellt neu
    snprintf(was, sizeof(was), "nach dem Klopfen zaehlt er %d", k);
    pruefe(was, remind_snooze_count(480) == k);
    pruefe("  und klopft nicht nochmal", aufschub_wecker(480) == NULL);
  }
  pruefe("nach dem dritten ist keiner mehr uebrig", !remind_snooze_left(480));
  pruefe("die Runde 12:30 hat ihre eigenen drei", remind_snooze_count(750) == 0 && remind_snooze_left(750));

  // Morgen ist die Runde 08:00 ein neuer Anlass.
  stub_jetzt = ortszeit(2026, 7, 15, 8, 0);
  remind_schedule();
  pruefe("morgen faengt die Runde 08:00 bei null an", remind_snooze_count(480) == 0 && remind_snooze_left(480));
  remind_snooze(480);
  pruefe("  und zaehlt dann 1", remind_snooze_count(480) == 1);
  remind_snooze_clear();
  pruefe("Abhaken und Wegdruecken loeschen ihn", remind_snooze_count(480) == 0);

  // Ein Aufschub, den 0.15.0 hinterliess (ohne Tag): er klopft noch, zaehlt
  // aber nicht - lieber einmal zu oft aufschieben als zu frueh verfallen.
  frisch(ortszeit(2026, 7, 14, 8, 5));
  plan_setzen(zwei, 2);
  persist_write_int(FACH_AUFSCHUB_ZEIT, (int)ortszeit(2026, 7, 14, 8, 15));
  persist_write_int(FACH_AUFSCHUB_MINUTE, 480);
  persist_write_int(FACH_AUFSCHUB_ZAHL, 3);
  remind_schedule();
  pruefe("0.15.0-Aufschub ohne Tag: er klopft um 08:15", aufschub_wecker(480) && ist_um(aufschub_wecker(480)->zeit, 2026, 7, 14, 8, 15));
  pruefe("  und laesst noch Aufschuebe uebrig", remind_snooze_left(480));
  remind_snooze_clear();
}

int main(void) {
  printf("Zeitzone: %s\n", getenv("TZ") ? getenv("TZ") : "(Rechner)");
  abschnitt_vorausplanen();
  abschnitt_neuplanen();
  abschnitt_umstellung();
  abschnitt_aufschub();
  abschnitt_aufschub_zaehlt();
  printf("%s\n", s_fehler ? "NICHT BESTANDEN" : "alles bestanden");
  return s_fehler ? 1 : 0;
}
