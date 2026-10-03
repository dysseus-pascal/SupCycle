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
//
// Exitcode 0 = alles wie zugesagt.
#include <pebble.h>
#include "plan.h"

time_t stub_jetzt;

// --- Persist im Speicher ---
#define FAECHER 8
static struct { bool da; uint8_t daten[256]; size_t laenge; } s_persist[FAECHER];

bool persist_exists(uint32_t key) { return key < FAECHER && s_persist[key].da; }
int persist_read_data(uint32_t key, void *buf, size_t size) {
  if (!persist_exists(key)) return -1;
  const size_t n = s_persist[key].laenge < size ? s_persist[key].laenge : size;
  memcpy(buf, s_persist[key].daten, n);
  return (int)n;
}
int32_t persist_read_int(uint32_t key) {
  int32_t v = 0;
  if (persist_exists(key)) memcpy(&v, s_persist[key].daten, sizeof(v));
  return v;
}
int persist_write_data(uint32_t key, const void *data, size_t size) {
  s_persist[key].da = true;
  s_persist[key].laenge = size;
  memcpy(s_persist[key].daten, data, size);
  return (int)size;
}
int persist_write_int(uint32_t key, int32_t value) { return persist_write_data(key, &value, sizeof(value)); }
int persist_delete(uint32_t key) { s_persist[key].da = false; return 0; }

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
  persist_delete(4);
  plan_set_taken(3, true);
  persist_delete(4);
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

  printf("%s\n", s_fehler ? "NICHT BESTANDEN" : "alles bestanden");
  return s_fehler ? 1 : 0;
}
