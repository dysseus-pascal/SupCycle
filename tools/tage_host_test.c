// Tage, Zyklen und der gespeicherte Plan (kalender.c, plan.c, cycle.c) - auf
// dem Rechner, in jeder Zeitzone, die das Skript vorgibt.
//
//   sh tools/tage_host_test.sh      (laeuft in Zuerich, London, New York, UTC)
//
// Was hier leicht falsch und teuer ist:
//
//   - DER TAG IST EIN DATUM (Audit M1). Bis 0.15.0 zaehlte die Uhr die
//     Ortsmitternacht durch 86400: in London hatten Samstag und Sonntag des
//     Fruehjahrswechsels dieselbe Nummer, und ein Haken vom Samstag stand am
//     Sonntag noch als genommen da.
//   - DIE UMSTELLUNG DARF NICHTS VERLIEREN. Wer aktualisiert, hat heute schon
//     abgehakt und steht mitten in einem Zyklus: beides muss danach genauso
//     dastehen - in Zuerich (Versatz +1), New York (0) und London (je nach
//     Jahreszeit).
//
// Die Erwartungen kommen aus der C-Bibliothek des Rechners (timegm, mktime),
// nicht aus kalender.c - sonst pruefte sich die Rechnung an sich selbst.
//
// Exitcode 0 = alles wie zugesagt.
#define _DEFAULT_SOURCE
#include <pebble.h>
#include <stdlib.h>
#include "cycle.h"
#include "cycle_selftest.h"
#include "kalender.h"
#include "plan.h"

time_t stub_jetzt;

static int s_fehler;
static void pruefe(const char *was, bool ok) {
  printf("%s %s\n", ok ? "  ok    " : "  FEHLER", was);
  if (!ok) s_fehler++;
}

// Persist-Faecher aus plan.c - hier mit Wert festgenagelt: eine
// Aktualisierung liest, was die Fassung davor schrieb.
#define FACH_PLAN 1
#define FACH_TAG 2
#define FACH_HAKEN 3
#define FACH_TAGE 9

// Ortszeit nach der C-Bibliothek (mktime kennt die Zeitzone des Rechners).
static time_t ortszeit(int j, int mo, int t, int h, int mi) {
  struct tm tm = { .tm_year = j - 1900, .tm_mon = mo - 1, .tm_mday = t,
                   .tm_hour = h, .tm_min = mi, .tm_isdst = -1 };
  return mktime(&tm);
}
// Der Kalendertag eines Datums, ueber UTC gerechnet.
static int32_t datum_tag(int j, int mo, int t) {
  struct tm tm = { .tm_year = j - 1900, .tm_mon = mo - 1, .tm_mday = t };
  return (int32_t)(timegm(&tm) / 86400);
}
// So zaehlte 0.15.0 (plan.c bis dahin): die Uhrzeit abziehen, durch 86400.
// Nur als Eingabe fuer die Umstellung - so stehen die Tage im Persist einer
// alten Uhr, an Umstellungstagen samt ihrem Sprung.
static int32_t tag_bis_015(time_t t) {
  const struct tm lt = *localtime(&t);
  return (int32_t)((t - (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec)) / 86400);
}

// Ein Platz im 26-Byte-Format (plan.h), von Hand.
static void platz(uint8_t *p, const char *name, int every, int on, int off, int32_t anker) {
  memset(p, 0, SC_ITEM_BYTES);
  memcpy(p, name, strlen(name));
  p[16] = 8; p[17] = 0; p[18] = 1;
  p[19] = (uint8_t)every; p[20] = (uint8_t)on; p[21] = (uint8_t)off;
  for (int b = 0; b < 4; b++) p[22 + b] = (uint8_t)((uint32_t)anker >> (8 * b));
}
static int32_t anker_im_persist(int i) {
  uint8_t b[SC_MAX_ITEMS * SC_ITEM_BYTES];
  persist_read_data(FACH_PLAN, b, sizeof(b));
  const uint8_t *p = b + i * SC_ITEM_BYTES;
  return (int32_t)((uint32_t)p[22] | ((uint32_t)p[23] << 8) | ((uint32_t)p[24] << 16) | ((uint32_t)p[25] << 24));
}

static void abschnitt_datum(void) {
  printf("\nKalendertag aus dem Datum\n");
  const int faelle[][3] = { { 1970, 1, 1 }, { 1969, 12, 31 }, { 2000, 2, 29 }, { 2000, 3, 1 },
                            { 2026, 3, 29 }, { 2026, 10, 25 }, { 2027, 3, 28 }, { 2100, 3, 1 } };
  for (unsigned i = 0; i < sizeof(faelle) / sizeof(faelle[0]); i++) {
    char was[64];
    snprintf(was, sizeof(was), "%04d-%02d-%02d", faelle[i][0], faelle[i][1], faelle[i][2]);
    pruefe(was, kalender_tag_aus_datum(faelle[i][0], faelle[i][1], faelle[i][2]) ==
                datum_tag(faelle[i][0], faelle[i][1], faelle[i][2]));
  }
}

// plan_today um 00:30, 12:00 und 23:30 an aufeinanderfolgenden Tagen ueber
// ein Umstellungswochenende: immer das Datum, und mittags genau +1 je Tag.
static void woche(int j, int mo, int t0) {
  int32_t mittag_vorher = 0;
  bool alle = true, plus_eins = true;
  for (int k = 0; k < 9; k++) {
    const int t = t0 + k;
    const int32_t soll = datum_tag(j, mo, t);
    const int uhrzeiten[][2] = { { 0, 30 }, { 12, 0 }, { 23, 30 } };
    for (int u = 0; u < 3; u++) {
      stub_jetzt = ortszeit(j, mo, t, uhrzeiten[u][0], uhrzeiten[u][1]);
      if (plan_today() != soll) {
        printf("           %04d-%02d-%02d %02d:%02d: %d statt %d\n", j, mo, t,
               uhrzeiten[u][0], uhrzeiten[u][1], (int)plan_today(), (int)soll);
        alle = false;
      }
    }
    stub_jetzt = ortszeit(j, mo, t, 12, 0);
    if (k > 0 && plan_today() != mittag_vorher + 1) plus_eins = false;
    mittag_vorher = plan_today();
  }
  char was[96];
  snprintf(was, sizeof(was), "ab %04d-%02d-%02d: jeder Tag sein Datum, morgens wie abends", j, mo, t0);
  pruefe(was, alle);
  snprintf(was, sizeof(was), "ab %04d-%02d-%02d: mittags genau +1 je Tag", j, mo, t0);
  pruefe(was, plus_eins);
}

static void abschnitt_heute(void) {
  printf("\nplan_today ist das Datum, auch an den Umstellungstagen\n");
  attrappe_persist_leeren();
  woche(2026, 3, 25);     // Umstellung 29.03.2026 (Europa), 08.03. (USA) davor
  woche(2026, 10, 21);    // 25.10.2026 (Europa)
  woche(2026, 10, 30);    // 01.11.2026 (USA)
  woche(2027, 3, 24);     // 28.03.2027 (Europa)
}

static void abschnitt_haken_london(void) {
  printf("\nEin Haken gilt seinem Tag (London-Fall)\n");
  // In London stand ein Haken vom Samstag 27.03.2027 am Sonntag noch da:
  // beide Tage hatten dieselbe Nummer.
  attrappe_persist_leeren();
  stub_jetzt = ortszeit(2027, 3, 27, 9, 0);
  plan_init();
  plan_set_taken(0, true);
  stub_jetzt = ortszeit(2027, 3, 28, 12, 0);
  plan_init();
  pruefe("Haken vom Samstag ist am Sonntag weg", !plan_taken(0));
  attrappe_persist_leeren();
  stub_jetzt = ortszeit(2026, 10, 25, 0, 30);
  plan_init();
  plan_set_taken(1, true);
  stub_jetzt = ortszeit(2026, 10, 25, 12, 0);
  pruefe("Haken von 00:30 steht am selben Tag um 12:00 noch", plan_taken(1));
  stub_jetzt = ortszeit(2026, 10, 26, 0, 30);
  pruefe("und ist am Montag weg", !plan_taken(1));
}

// Eine Uhr mit dem Persist von 0.15.0, abgehakt um `jetzt`.
static void alte_uhr(time_t jetzt, int32_t tag_alt, uint8_t haken) {
  attrappe_persist_leeren();
  uint8_t b[SC_MAX_ITEMS * SC_ITEM_BYTES];
  memset(b, 0, sizeof(b));
  const int32_t heute_alt = tag_bis_015(jetzt);
  // Platz 1: alle 2 Tage ab heute - heute faellig.
  platz(b, "Zink", 2, 0, 0, heute_alt);
  // Platz 2: 1 Woche an, 1 aus, Anker vor 7 Tagen - heute Pause.
  platz(b + SC_ITEM_BYTES, "Maca", 1, 1, 1, heute_alt - 7);
  // Platz 3: alle 2 Tage ab gestern - heute nicht faellig.
  platz(b + 2 * SC_ITEM_BYTES, "Eisen", 2, 0, 0, heute_alt - 1);
  persist_write_data(FACH_PLAN, b, sizeof(b));
  persist_write_int(FACH_TAG, tag_alt);
  persist_write_int(FACH_HAKEN, haken);
  stub_jetzt = jetzt;
}

static void umstellung_um(time_t jetzt, const char *wann) {
  char was[128];
  const int32_t heute_alt = tag_bis_015(jetzt);
  const int32_t versatz = kalender_tag(jetzt) - heute_alt;
  struct tm lt = *localtime(&jetzt);
  const int32_t heute = datum_tag(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);

  alte_uhr(jetzt, heute_alt, 0x1);
  plan_init();
  snprintf(was, sizeof(was), "%s: heute abgehakt bleibt abgehakt", wann);
  pruefe(was, plan_taken(0));
  snprintf(was, sizeof(was), "%s: gemerkter Tag ist jetzt das Datum", wann);
  pruefe(was, persist_read_int(FACH_TAG) == heute);
  snprintf(was, sizeof(was), "%s: alle 2 Tage - heute faellig wie vorher", wann);
  pruefe(was, plan_due_today(0));
  snprintf(was, sizeof(was), "%s: 1 an/1 aus - heute Pause wie vorher", wann);
  pruefe(was, !plan_due_today(1));
  snprintf(was, sizeof(was), "%s: alle 2 Tage ab gestern - heute nicht, wie vorher", wann);
  pruefe(was, !plan_due_today(2));
  snprintf(was, sizeof(was), "%s: Anker im Persist um den Versatz verschoben", wann);
  pruefe(was, anker_im_persist(0) == heute_alt + versatz && anker_im_persist(1) == heute_alt - 7 + versatz);
  snprintf(was, sizeof(was), "%s: Fassung im Persist", wann);
  pruefe(was, persist_read_int(FACH_TAGE) == 2);
  // Ein zweiter Start verschiebt nichts mehr.
  plan_init();
  snprintf(was, sizeof(was), "%s: zweiter Start verschiebt nicht noch einmal", wann);
  pruefe(was, plan_taken(0) && anker_im_persist(0) == heute_alt + versatz && persist_read_int(FACH_TAG) == heute);
  // Der naechste Tag beginnt um Mitternacht.
  stub_jetzt = ortszeit(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday + 1, 0, 1);
  snprintf(was, sizeof(was), "%s: am naechsten Tag ist der Haken weg", wann);
  pruefe(was, !plan_taken(0));
  snprintf(was, sizeof(was), "%s: am naechsten Tag: 1 an/1 aus weiter Pause, alle 2 Tage ab gestern faellig", wann);
  pruefe(was, !plan_due_today(1) && plan_due_today(2));

  // Gestern abgehakt: der Haken gilt nicht fuer heute.
  alte_uhr(jetzt, heute_alt - 1, 0x1);
  plan_init();
  snprintf(was, sizeof(was), "%s: gestern abgehakt bleibt gestern", wann);
  pruefe(was, !plan_taken(0));
  // Die Uhr steht hinter dem gemerkten Tag: fraglich bleibt fraglich.
  alte_uhr(jetzt, heute_alt + 1, 0x1);
  plan_init();
  snprintf(was, sizeof(was), "%s: Uhr hinter dem gemerkten Tag: weiter fraglich, Haken bleibt", wann);
  pruefe(was, plan_uhr_fraglich() && plan_taken(0) && persist_read_int(FACH_TAG) == heute + 1);
}

static void abschnitt_umstellung(void) {
  printf("\nUmstellung von 0.15.0: nichts geht verloren\n");
  umstellung_um(ortszeit(2026, 1, 14, 9, 0), "Winter 09:00");
  umstellung_um(ortszeit(2026, 7, 14, 9, 0), "Sommer 09:00");
  umstellung_um(ortszeit(2026, 7, 14, 23, 50), "Sommer 23:50");
  umstellung_um(ortszeit(2026, 10, 25, 0, 30), "Umstellungstag 25.10. 00:30");
  umstellung_um(ortszeit(2026, 10, 25, 12, 0), "Umstellungstag 25.10. 12:00");
  umstellung_um(ortszeit(2026, 3, 29, 12, 0), "Umstellungstag 29.03. 12:00");
  // Eine frische Uhr: nichts zu verschieben, aber die Fassung steht.
  attrappe_persist_leeren();
  stub_jetzt = ortszeit(2026, 7, 14, 9, 0);
  plan_init();
  pruefe("frische Uhr: Fassung steht, der Tag ist das Datum",
         persist_read_int(FACH_TAGE) == 2 && persist_read_int(FACH_TAG) == datum_tag(2026, 7, 14));
}

static void abschnitt_gespeichert(void) {
  printf("\nDer gespeicherte Plan (N2)\n");
  // Sieben Eintraege im alten 25-Byte-Format (175 Byte): gelesen werden
  // sechs, gespeichert muss genau das werden, was gelesen wurde.
  attrappe_persist_leeren();
  stub_jetzt = ortszeit(2026, 7, 14, 9, 0);
  plan_init();
  const char *namen[] = { "Zink", "Magnesium", "Eisen", "Kreatin", "Vitamin D3", "Omega 3", "Selen" };
  uint8_t alt[7 * SC_ITEM_BYTES_V1];
  memset(alt, 0, sizeof(alt));
  for (int i = 0; i < 7; i++) {
    uint8_t *p = alt + i * SC_ITEM_BYTES_V1;
    memcpy(p, namen[i], strlen(namen[i]));
    p[16] = 8; p[18] = 1;      // mode 1: taeglich
  }
  pruefe("sieben alte Eintraege werden gelesen", plan_set_from_bytes(alt, sizeof(alt)) && plan_count() == 6);
  pruefe("gespeichert sind 156 Byte", persist_get_size(FACH_PLAN) == SC_MAX_ITEMS * SC_ITEM_BYTES);
  plan_init();
  bool gleich = plan_count() == 6;
  for (int i = 0; i < 6; i++) gleich = gleich && strcmp(plan_item(i)->name, namen[i]) == 0;
  pruefe("nach dem Neustart dieselben sechs Namen", gleich);
}

static void abschnitt_kur(void) {
  printf("\nEinnahmewochen ohne Pause: einmalige Kur (N6)\n");
  const int32_t a = datum_tag(2026, 7, 1);
  CycleState c = cycle_state(a, a + 500, 4, 0);
  pruefe("4 Wochen ohne Pause, Tag 500: vorbei, nicht Woche 72 von 4",
         c.phase == CyclePhaseDone && c.week <= c.of_weeks && !cycle_active_today(a, a + 500, 4, 0));
  c = cycle_state(a, a + 10, 4, 0);
  pruefe("Tag 10: Woche 2 von 4, noch 18 Tage", c.phase == CyclePhaseOn && c.week == 2 && c.of_weeks == 4 && c.days_left == 18);
  pruefe("Tag 27 aktiv, Tag 28 nicht", cycle_active_today(a, a + 27, 4, 0) && !cycle_active_today(a, a + 28, 4, 0));
  // Und im Plan: nach der Kur nicht mehr faellig.
  attrappe_persist_leeren();
  stub_jetzt = ortszeit(2026, 7, 1, 9, 0);
  plan_init();
  uint8_t b[SC_MAX_ITEMS * SC_ITEM_BYTES];
  memset(b, 0, sizeof(b));
  platz(b, "Ashwagandha", 1, 4, 0, a);
  plan_set_from_bytes(b, sizeof(b));
  pruefe("im Plan: Tag 27 faellig, Tag 28 nicht", plan_due_on(0, a + 27) && !plan_due_on(0, a + 28));
  // Die Pruefungen, die sonst im Emulator laufen (tools/selftest.sh), hier
  // auch - mit denselben Erwartungen.
  pruefe("Selbsttest der Zyklusrechnung (cycle_selftest.c) ohne Fehler", cycle_selftest_run() == 0);
}

int main(void) {
  printf("Zeitzone: %s\n", getenv("TZ") ? getenv("TZ") : "(Rechner)");
  abschnitt_datum();
  abschnitt_heute();
  abschnitt_haken_london();
  abschnitt_umstellung();
  abschnitt_gespeichert();
  abschnitt_kur();
  printf("%s\n", s_fehler ? "NICHT BESTANDEN" : "alles bestanden");
  return s_fehler ? 1 : 0;
}
