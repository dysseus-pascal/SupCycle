// Hakenzeiten und das Zuruecknehmen von Haken (plan.c) - auf dem Rechner.
//
//   sh tools/plan_host_test.sh
//
// Was hier leicht falsch und teuer ist:
//
//   - Die HAKENZEIT muss die des ersten Abhakens bleiben. Die Erinnerung hakt
//     die ganze Runde ab; ueberschriebe sie die Zeit, wanderte eine Einnahme
//     von 08:00 in der Akte auf 08:30.
//   - Sie muss einen Neustart der App ueberstehen und um Mitternacht mit den
//     Haken verschwinden.
//   - Der Befehl des Telefons darf NUR den Haken nehmen, dessen Einnahme dort
//     geloescht wurde. Ein inzwischen neu gesetzter traegt eine andere Zeit
//     und bleibt.
//   - RUECKWAERTS (0.13.3): steht die Uhr nach einem Neustart zurueck -
//     Stunden oder einen Tag -, bleiben die Haken des echten Tages. Auch einer,
//     den man WAEHREND dieser Zeit setzt (ein Zwischenstand nach 0.14.0 verlor
//     ihn beim Stellen der Uhr).
//   - VORLAUF (Audit M10): stand die Uhr ein bis zwei Tage vor und wurde
//     zurueckgestellt, sind die Haken des echten Tages am Folgetag weg -
//     sobald das Telefon die zurueckgestellte Uhr bestaetigt hat. Ohne das
//     Telefon kann die Uhr Vorlauf und Neustart nicht unterscheiden.
//   - BESTAETIGEN: hoechstens 300 s Abweichung und derselbe Tag.
//   - Ein Haken OHNE BRAUCHBARE ZEIT (vor 0.14.0 gesetzt, oder als die Uhr
//     noch im Jahr 2000 stand) faellt wie jeder andere mit einem neuen Tag.
//
// Exitcode 0 = alles wie zugesagt.
#include <pebble.h>
#include "plan.h"
#include "prefs.h"

time_t stub_jetzt;

// Persist liegt im Speicher: tools/host/attrappe_persist.c.

static int s_fehler;
static void pruefe(const char *was, bool ok) {
  printf("%s %s\n", ok ? "  ok    " : "  FEHLER", was);
  if (!ok) s_fehler++;
}

// Ein Datenblock mit Zeiten je Platz, wie ihn das Telefon schickt.
static void zeiten(uint8_t *aus, const uint32_t *t) {
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    for (int b = 0; b < 4; b++) aus[i * 4 + b] = (uint8_t)(t[i] >> (8 * b));
  }
}

// 03.10.2026, 08:00 UTC - der Test laeuft mit TZ=UTC.
#define MORGEN 1791014400
#define TAG 86400
#define STUNDE 3600
// Mitternacht vor MORGEN (03.10.2026 00:00 UTC) und Tagesnummern dazu.
#define MITTERNACHT (MORGEN - 8 * STUNDE)
#define TAG_0310 20729
// Persist-Faecher aus plan.c - hier mit Wert festgenagelt: das Telefon und
// eine Aktualisierung lesen dieselben.
#define FACH_TAG 2
#define FACH_HAKEN 3
#define FACH_ZEITEN 8
// Fach 4 gehoert prefs.c (Animation); 0.14.0 legte die Zeiten auch dorthin.
#define FACH_FX 4
#define FACH_ZEITEN_014 4

// Frischer Zustand: leerer Persist, App neu gestartet.
static void frisch(time_t jetzt) {
  attrappe_persist_leeren();
  stub_jetzt = jetzt;
  plan_init();
}

int main(void) {
  stub_jetzt = MORGEN;
  plan_init();

  // Abhaken setzt die Zeit, nochmals Abhaken laesst sie stehen.
  plan_set_taken(0, true);
  pruefe("Haken traegt seine Zeit", plan_taken_at(0) == MORGEN);
  stub_jetzt = MORGEN + 1800;
  plan_set_taken(0, true);
  pruefe("erneutes Abhaken behaelt die erste Zeit", plan_taken_at(0) == MORGEN);
  plan_set_taken(2, true);
  pruefe("offener Platz hat keine Zeit", plan_taken_at(1) == 0);

  // Der Datenblock ans Telefon: little endian, Platz fuer Platz.
  uint8_t block[SC_TAKEN_AT_BYTES];
  pruefe("Datenblock hat 24 Byte", plan_taken_at_to_bytes(block) == 24);
  const uint32_t z0 = (uint32_t)block[0] | ((uint32_t)block[1] << 8) | ((uint32_t)block[2] << 16) | ((uint32_t)block[3] << 24);
  const uint32_t z2 = (uint32_t)block[8] | ((uint32_t)block[9] << 8) | ((uint32_t)block[10] << 16) | ((uint32_t)block[11] << 24);
  pruefe("Platz 1 im Block", z0 == MORGEN);
  pruefe("Platz 3 im Block", z2 == MORGEN + 1800);

  // Ein Neustart der App liest Haken und Zeiten wieder.
  plan_init();
  pruefe("Zeit uebersteht den Neustart", plan_taken(0) && plan_taken_at(0) == MORGEN);

  // Wieder abwaehlen nimmt die Zeit mit.
  plan_set_taken(2, false);
  pruefe("abgewaehlter Haken hat keine Zeit", plan_taken_at(2) == 0);

  // Der Befehl des Telefons: nur derselbe Haken.
  plan_set_taken(1, true);                 // 08:30
  stub_jetzt = MORGEN + 3600;
  uint32_t gemeint[SC_MAX_ITEMS] = { MORGEN, MORGEN, 0, 0, 0, 0 };
  zeiten(block, gemeint);
  // Platz 1 passt, Platz 2 wurde um 08:30 gesetzt, das Telefon meint 08:00.
  pruefe("Befehl aendert etwas", plan_untake(0x3, block, sizeof(block)));
  pruefe("passender Haken ist weg", !plan_taken(0) && plan_taken_at(0) == 0);
  pruefe("neu gesetzter Haken bleibt", plan_taken(1) && plan_taken_at(1) == MORGEN + 1800);
  pruefe("nichts mehr zu tun", !plan_untake(0x3, block, sizeof(block)));
  // Ohne Zeit fuer den Platz kein Zuruecknehmen.
  uint32_t richtig[SC_MAX_ITEMS] = { 0, MORGEN + 1800, 0, 0, 0, 0 };
  zeiten(block, richtig);
  pruefe("zu kurzer Block nimmt nichts", !plan_untake(0x2, block, 4) && plan_taken(1));
  pruefe("Platz ausserhalb der Maske bleibt", !plan_untake(0x1, block, sizeof(block)) && plan_taken(1));
  pruefe("mit der richtigen Zeit geht er", plan_untake(0x2, block, sizeof(block)) && !plan_taken(1));
  plan_init();
  pruefe("Zuruecknehmen uebersteht den Neustart", !plan_taken(0) && !plan_taken(1));

  // Ein Haken von vor dieser Fassung: keine Zeit im Persist, die Zeit ist 0.
  persist_delete(FACH_ZEITEN);
  plan_set_taken(3, true);
  persist_delete(FACH_ZEITEN);
  plan_init();
  pruefe("alter Haken bleibt, ohne Zeit", plan_taken(3) && plan_taken_at(3) == 0);
  uint32_t ohne[SC_MAX_ITEMS] = { 0 };
  zeiten(block, ohne);
  pruefe("alter Haken laesst sich mit Zeit 0 zuruecknehmen", plan_untake(0x8, block, sizeof(block)) && !plan_taken(3));

  // Ein umbenannter Platz verliert Haken und Zeit.
  uint8_t plan[SC_MAX_ITEMS * SC_ITEM_BYTES];
  memset(plan, 0, sizeof(plan));
  memcpy(plan, "Zink", 4);
  plan[18] = 1; plan[19] = 1;
  plan_set_from_bytes(plan, sizeof(plan));
  plan_set_taken(0, true);
  memcpy(plan, "Eisen", 5);
  plan_set_from_bytes(plan, sizeof(plan));
  pruefe("umbenannt: Haken und Zeit weg", !plan_taken(0) && plan_taken_at(0) == 0);

  // Um Mitternacht verschwinden die Zeiten mit den Haken.
  plan_set_taken(0, true);
  stub_jetzt = MORGEN + 86400;
  pruefe("neuer Tag: keine Haken", !plan_taken(0));
  plan_set_taken(0, true);
  pruefe("neuer Tag: neue Zeit", plan_taken_at(0) == MORGEN + 86400);

  printf("\nHaken ohne brauchbare Zeit\n");
  // Ein Haken von vor 0.14.0 (keine Zeit) haengt am gemerkten Tag.
  frisch(MORGEN);
  plan_set_taken(3, true);
  persist_delete(FACH_ZEITEN);
  plan_init();
  stub_jetzt = MORGEN - TAG;
  pruefe("ohne Zeit, einen Tag zurueck: bleibt", plan_taken(3));
  stub_jetzt = MORGEN + 2 * STUNDE;
  pruefe("ohne Zeit, wieder richtig: bleibt", plan_taken(3));
  stub_jetzt = MITTERNACHT + TAG;
  pruefe("ohne Zeit, neuer Tag: weg", !plan_taken(3));
  // Gesetzt, als die Uhr nach einem Neustart noch im Jahr 2000 stand.
  frisch(MORGEN);
  stub_jetzt = 946728000;                           // 01.01.2000 12:00
  plan_set_taken(0, true);
  pruefe("im Jahr 2000 gesetzt: traegt diese Zeit", plan_taken_at(0) == 946728000);
  stub_jetzt = MORGEN + STUNDE;                     // das Telefon stellt die Uhr
  pruefe("im Jahr 2000 gesetzt, Uhr gestellt: bleibt", plan_taken(0));
  stub_jetzt = MITTERNACHT + TAG + STUNDE;
  pruefe("im Jahr 2000 gesetzt: faellt mit dem neuen Tag", !plan_taken(0));
  // Haken haengen am gemerkten Tag, nicht an ihrer Zeit: auch eine Zeit, die
  // vor dem heutigen Tag liegt, nimmt den Haken nicht weg.
  {
    uint32_t z[SC_MAX_ITEMS] = { 1735689599u, MITTERNACHT - 1, 0, 0, 0, 0 };
    attrappe_persist_leeren();
    persist_write_int(FACH_TAG, TAG_0310);
    persist_write_int(FACH_HAKEN, 0x3);
    persist_write_data(FACH_ZEITEN, z, sizeof(z));
    stub_jetzt = MORGEN;
    plan_init();
    pruefe("Zeit 31.12.2024: Haken des gemerkten Tages bleibt", plan_taken(0));
    pruefe("Zeit 02.10. 23:59:59 am gemerkten 03.10.: Haken bleibt", plan_taken(1));
  }

  printf("\nFach 4 gehoert der Animation, die Zeiten liegen in Fach 8\n");
  // Bis 0.14.0 schrieben plan.c und prefs.c beide in Fach 4.
  frisch(MORGEN);
  prefs_init();
  prefs_set_fx(false);
  prefs_set_fx(true);
  plan_set_taken(0, true);
  pruefe("Hakenzeiten in Fach 8 (24 Byte)", persist_get_size(FACH_ZEITEN) == 24);
  pruefe("Fach 4 bleibt die Zahl der Animation", persist_get_size(FACH_FX) == 4 && persist_read_int(FACH_FX) == 1);
  prefs_set_fx(false);
  plan_init();
  prefs_init();
  pruefe("Animation umgeschaltet: die Hakenzeit bleibt", plan_taken_at(0) == MORGEN);
  pruefe("und die Animation ist aus", !prefs_fx());
  prefs_set_fx(true);
  plan_set_taken(0, false);                          // keine Haken: Zeiten alle 0
  plan_init();
  prefs_init();
  pruefe("ohne Haken bleibt die Animation an", prefs_fx());
  // Was 0.14.0 hinterliess: die Zeiten (24 Byte) in Fach 4.
  {
    uint32_t z[SC_MAX_ITEMS] = { 0, MORGEN, 0, 0, 0, 0 };
    attrappe_persist_leeren();
    persist_write_int(FACH_TAG, TAG_0310);
    persist_write_int(FACH_HAKEN, 0x2);
    persist_write_data(FACH_ZEITEN_014, z, sizeof(z));
    stub_jetzt = MORGEN + STUNDE;
    prefs_init();
    pruefe("0.14.0: erste Zeit 0 schaltet die Animation nicht aus", prefs_fx());
    plan_init();
    pruefe("0.14.0: die Zeiten kommen mit", plan_taken(1) && plan_taken_at(1) == MORGEN);
    pruefe("0.14.0: jetzt in Fach 8", persist_get_size(FACH_ZEITEN) == 24);
    pruefe("0.14.0: Fach 4 geraeumt", !persist_exists(FACH_FX));
    // Und eine Zahl in Fach 4 (die Animation) ist keine Zeit.
    attrappe_persist_leeren();
    persist_write_int(FACH_TAG, TAG_0310);
    persist_write_int(FACH_HAKEN, 0x1);
    persist_write_int(FACH_FX, 0);
    prefs_init();
    plan_init();
    pruefe("Zahl in Fach 4: Animation aus, wie gespeichert", !prefs_fx() && persist_read_int(FACH_FX) == 0);
    pruefe("Zahl in Fach 4: kein Zeitenblock, Haken ohne Zeit", plan_taken(0) && plan_taken_at(0) == 0);
    // Erst ein zweiter Start zeigt, ob das Fach stehen blieb: persist_read_int
    // liest auch ein geloeschtes Fach als 0 - "aus".
    pruefe("Zahl in Fach 4: das Fach steht noch, 4 Byte", persist_exists(FACH_FX) && persist_get_size(FACH_FX) == 4);
    prefs_init();
    plan_init();
    pruefe("Zahl in Fach 4, zweiter Start: Animation weiter aus", !prefs_fx());
    // Auch "an" (1) ist eine Zahl und bleibt.
    persist_write_int(FACH_FX, 1);
    prefs_init();
    plan_init();
    prefs_init();
    pruefe("Zahl 1 in Fach 4, zweimal gestartet: Animation an, Fach steht", prefs_fx() && persist_get_size(FACH_FX) == 4);
  }

  printf("\nRueckwaerts (0.13.3)\n");
  frisch(MORGEN);
  plan_set_taken(0, true);
  stub_jetzt = MORGEN - 2 * STUNDE;                 // Zeitzone neu, am selben Tag
  pruefe("2 h zurueck: Haken bleibt", plan_taken(0) && plan_taken_at(0) == MORGEN);
  stub_jetzt = MITTERNACHT - 2 * STUNDE;            // ueber Mitternacht zurueck
  pruefe("ueber Mitternacht zurueck: Haken bleibt", plan_taken(0));
  pruefe("ueber Mitternacht zurueck: gemerkter Tag bleibt", persist_read_int(FACH_TAG) == TAG_0310);
  plan_init();
  pruefe("Neustart mit falscher Zeit: Haken bleibt", plan_taken(0));
  stub_jetzt = MORGEN + 2 * STUNDE;                 // das Telefon stellt die Zeit
  pruefe("Zeit wieder richtig: Haken mit seiner Zeit", plan_taken(0) && plan_taken_at(0) == MORGEN);
  stub_jetzt = 946728000;                           // 01.01.2000: Uhr ohne Zeit
  pruefe("Uhr im Jahr 2000: Haken bleibt", plan_taken(0));
  pruefe("Uhr im Jahr 2000: gemerkter Tag bleibt", persist_read_int(FACH_TAG) == TAG_0310);
  stub_jetzt = MORGEN + 3 * STUNDE;
  pruefe("aus dem Jahr 2000 zurueck: Haken bleibt", plan_taken(0));
  stub_jetzt = MORGEN - 3 * TAG;                    // drei Tage zurueck, plausibel
  pruefe("drei Tage zurueck: Haken bleibt", plan_taken(0));
  pruefe("drei Tage zurueck: der gemerkte Tag war falsch, heute gilt",
         persist_read_int(FACH_TAG) == TAG_0310 - 3);
  // GRENZE (README): war es doch die Uhr, die drei Tage zurueckstand, ist
  // das Stellen fuer die Uhr ein neuer Tag - die Haken sind weg, wie seit 0.13.3.
  stub_jetzt = MORGEN + 4 * STUNDE;
  pruefe("drei Tage zurueck und wieder richtig: neuer Tag, Haken weg (Grenze)", !plan_taken(0));

  printf("\nRueckwaerts: abgehakt, waehrend die Uhr auf gestern steht\n");
  frisch(MORGEN);                                   // 03.10. 08:00
  plan_set_taken(1, true);
  stub_jetzt = MITTERNACHT - STUNDE;                // Neustart, Uhr auf 02.10. 23:00
  plan_init();
  pruefe("auf gestern: die Uhr fragt das Telefon", plan_uhr_fraglich());
  plan_set_taken(0, true);
  pruefe("auf gestern abgehakt: steht", plan_taken(0) && plan_taken(1));
  pruefe("auf gestern abgehakt: der gemerkte Tag bleibt", persist_read_int(FACH_TAG) == TAG_0310);
  // Das Telefon nennt seine (echte) Zeit, bevor es die Uhr gestellt hat.
  pruefe("Telefon 9 h voraus: nicht bestaetigt", !plan_uhr_bestaetigt(MORGEN + 20 * 60));
  pruefe("nicht bestaetigt: der gemerkte Tag bleibt", persist_read_int(FACH_TAG) == TAG_0310);
  stub_jetzt = MORGEN + 30 * 60;                    // das Telefon stellt die Uhr: 08:30
  plan_init();
  pruefe("Uhr gestellt: der auf gestern gesetzte Haken bleibt", plan_taken(0));
  pruefe("Uhr gestellt: und seine Zeit, wie gesetzt", plan_taken_at(0) == MITTERNACHT - STUNDE);
  pruefe("Uhr gestellt: der Haken davor bleibt", plan_taken(1) && plan_taken_at(1) == MORGEN);
  pruefe("Uhr gestellt: nicht mehr fraglich", !plan_uhr_fraglich());
  stub_jetzt = MITTERNACHT + TAG;                   // echter Folgetag 00:00
  pruefe("echter Folgetag: beide weg", !plan_taken(0) && !plan_taken(1));

  printf("\nPlausibel heisst ab 2025 und mehr als zwei Tage\n");
  frisch(MORGEN);
  plan_set_taken(0, true);
  stub_jetzt = MORGEN - 2 * TAG;                    // genau zwei Tage
  plan_taken(0);
  pruefe("zwei Tage zurueck: gemerkter Tag bleibt", persist_read_int(FACH_TAG) == TAG_0310);
  frisch(1735693200 + 3 * TAG);                     // 04.01.2025 01:00
  stub_jetzt = 1735693200;                          // 01.01.2025 01:00, Tag 20089
  plan_taken(0);
  pruefe("01.01.2025: plausibel, heute gilt", persist_read_int(FACH_TAG) == 20089);
  frisch(1735686000 + 3 * TAG);                     // 03.01.2025 23:00
  stub_jetzt = 1735686000;                          // 31.12.2024 23:00, Tag 20088
  plan_taken(0);
  pruefe("31.12.2024: nicht plausibel, gemerkter Tag bleibt", persist_read_int(FACH_TAG) == 20088 + 3);

  printf("\nVorlauf (M10): ohne Telefon bleibt der vorausgeeilte Tag\n");
  frisch(MORGEN);
  plan_set_taken(0, true);
  stub_jetzt = MORGEN + TAG + STUNDE;               // Uhr einen Tag voraus
  pruefe("einen Tag voraus: fuer die Uhr ein neuer Tag", !plan_taken(0));
  pruefe("einen Tag voraus: nicht fraglich", !plan_uhr_fraglich());
  plan_set_taken(0, true);                          // waehrend des Vorlaufs
  stub_jetzt = MORGEN + 90 * 60;                    // zurueckgestellt, 09:30
  plan_set_taken(1, true);                          // nach dem Zurueckstellen
  pruefe("zurueckgestellt: die Uhr fragt das Telefon", plan_uhr_fraglich());
  pruefe("zurueckgestellt: beide Haken stehen", plan_taken(0) && plan_taken(1));
  stub_jetzt = MITTERNACHT + TAG + 8 * STUNDE;      // echter Folgetag 08:00
  pruefe("ohne Bestaetigung: am echten Folgetag stehen beide noch (Grenze)", plan_taken(0) && plan_taken(1));

  printf("\nVorlauf (M10): das Telefon bestaetigt die zurueckgestellte Uhr\n");
  frisch(MORGEN);
  stub_jetzt = MORGEN + TAG + STUNDE;               // Uhr einen Tag voraus
  plan_set_taken(0, true);
  stub_jetzt = MORGEN + 90 * 60;                    // zurueckgestellt, 09:30
  plan_set_taken(1, true);
  pruefe("Telefonzeit passt: bestaetigt", plan_uhr_bestaetigt(MORGEN + 90 * 60));
  pruefe("bestaetigt: der gemerkte Tag ist heute", persist_read_int(FACH_TAG) == TAG_0310);
  pruefe("bestaetigt: beide Haken bleiben, mit ihren Zeiten",
         plan_taken(0) && plan_taken_at(0) == MORGEN + TAG + STUNDE && plan_taken(1) && plan_taken_at(1) == MORGEN + 90 * 60);
  pruefe("bestaetigt: nicht mehr fraglich", !plan_uhr_fraglich());
  pruefe("ein zweites Mal: nichts zu tun", !plan_uhr_bestaetigt(MORGEN + 90 * 60));
  plan_init();
  pruefe("Neustart am selben Tag: beide stehen", plan_taken(0) && plan_taken(1));
  stub_jetzt = MITTERNACHT + TAG + 8 * STUNDE;      // echter Folgetag 08:00
  pruefe("echter Folgetag: beide weg", !plan_taken(0) && !plan_taken(1));
  pruefe("echter Folgetag: auch im Persist", persist_read_int(FACH_HAKEN) == 0);
  plan_init();
  pruefe("echter Folgetag, Neustart: bleiben weg", !plan_taken(0) && !plan_taken(1));

  frisch(MORGEN);
  stub_jetzt = MORGEN + 2 * TAG;                    // zwei Tage voraus
  plan_taken(0);
  stub_jetzt = MORGEN + 4 * STUNDE;                 // zurueckgestellt, 12:00
  plan_set_taken(2, true);
  pruefe("zwei Tage voraus, zurueckgestellt: fraglich, Tag bleibt",
         plan_uhr_fraglich() && persist_read_int(FACH_TAG) == TAG_0310 + 2);
  pruefe("zwei Tage voraus: bestaetigt", plan_uhr_bestaetigt(MORGEN + 4 * STUNDE + 10));
  stub_jetzt = MITTERNACHT + TAG + 10 * STUNDE;     // echter Folgetag 10:00
  pruefe("zwei Tage voraus, bestaetigt: am echten Folgetag weg", !plan_taken(2));

  printf("\nBestaetigen: hoechstens 300 s Abweichung, derselbe Tag\n");
  // Vorgelaufen: gemerkt ist der 04.10., die Uhr steht auf dem 03.10. 08:00.
  frisch(MORGEN + TAG);
  stub_jetzt = MORGEN;
  pruefe("Telefon 301 s voraus: nicht", !plan_uhr_bestaetigt(MORGEN + 301) && persist_read_int(FACH_TAG) == TAG_0310 + 1);
  pruefe("die Abweichung steht im Log", strstr(attrappe_log_text, "Telefonzeit weicht -301 s ab") != NULL);
  pruefe("Telefon 301 s zurueck: nicht", !plan_uhr_bestaetigt(MORGEN - 301) && persist_read_int(FACH_TAG) == TAG_0310 + 1);
  pruefe("Telefon 300 s voraus: bestaetigt", plan_uhr_bestaetigt(MORGEN + 300) && persist_read_int(FACH_TAG) == TAG_0310);
  frisch(MORGEN + TAG);
  stub_jetzt = MORGEN;
  pruefe("Telefon 300 s zurueck: bestaetigt", plan_uhr_bestaetigt(MORGEN - 300) && persist_read_int(FACH_TAG) == TAG_0310);
  frisch(MORGEN + 2 * TAG);                         // gemerkt 05.10.
  stub_jetzt = MITTERNACHT + TAG - 60;              // Uhr 03.10. 23:59:00
  pruefe("90 s, aber Mitternacht dazwischen: nicht", !plan_uhr_bestaetigt(MITTERNACHT + TAG + 30));
  pruefe("30 s, derselbe Tag: bestaetigt", plan_uhr_bestaetigt(MITTERNACHT + TAG - 30) && persist_read_int(FACH_TAG) == TAG_0310);
  frisch(MORGEN);
  pruefe("Uhr auf dem gemerkten Tag: nicht fraglich", !plan_uhr_fraglich());
  pruefe("Uhr auf dem gemerkten Tag: Bestaetigen aendert nichts", !plan_uhr_bestaetigt(MORGEN));
  stub_jetzt = 946728000;                           // 01.01.2000: Uhr ohne Zeit
  pruefe("Uhr im Jahr 2000: nicht fraglich", !plan_uhr_fraglich());
  pruefe("Uhr im Jahr 2000: auch passende Telefonzeit gibt keinen Tag",
         !plan_uhr_bestaetigt(946728000) && persist_read_int(FACH_TAG) == TAG_0310);

  printf("\nMitternacht knapp\n");
  frisch(MITTERNACHT + TAG - 1);                    // 03.10. 23:59:59
  plan_set_taken(0, true);
  stub_jetzt = MITTERNACHT + TAG - 1;
  pruefe("23:59:59 gesetzt: steht noch um 23:59:59", plan_taken(0));
  stub_jetzt = MITTERNACHT + TAG;                   // 04.10. 00:00:00
  pruefe("um 00:00:00 weg", !plan_taken(0));
  plan_set_taken(1, true);
  pruefe("00:00:00 gesetzt: steht um 00:00:00", plan_taken(1));
  stub_jetzt = MITTERNACHT + 2 * TAG - 1;
  pruefe("00:00:00 gesetzt: steht bis 23:59:59", plan_taken(1));

  printf("%s\n", s_fehler ? "NICHT BESTANDEN" : "alles bestanden");
  return s_fehler ? 1 : 0;
}
