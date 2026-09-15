#include "cycle_selftest.h"

#ifdef SC_SELFTEST
#include <pebble.h>
#include "cycle.h"

// Prüft die Zyklusrechnung AUF DER UHR, also auf der Zielarchitektur.
//
// Warum nicht auf dem Baurechner: dort steht kein C-Compiler (kein gcc, ohne
// Passwort kein apt), nur der ARM-Compiler der SDK. Ein Test, der nie läuft,
// ist keiner.
//
// Warum überhaupt so gründlich: eine Zyklusrechnung, die um einen Tag oder
// eine Woche danebenliegt, fällt im Betrieb erst nach Wochen auf — und dann
// hat man schon falsch dosiert. Die Erwartungen unten sind von Hand
// nachgerechnet, nicht aus demselben Code gezogen.

static int s_fails;

#define EQ(name, got, want)                                                \
  do {                                                                     \
    const long g_ = (long)(got), w_ = (long)(want);                        \
    if (g_ == w_) {                                                        \
      APP_LOG(APP_LOG_LEVEL_INFO, "  ok     %s (%ld)", name, g_);          \
    } else {                                                               \
      APP_LOG(APP_LOG_LEVEL_ERROR, "  FEHLER %s: %ld statt %ld",           \
              name, g_, w_);                                               \
      s_fails++;                                                           \
    }                                                                      \
  } while (0)

#define OK(name, cond)                                                     \
  do {                                                                     \
    if (cond) {                                                            \
      APP_LOG(APP_LOG_LEVEL_INFO, "  ok     %s", name);                    \
    } else {                                                               \
      APP_LOG(APP_LOG_LEVEL_ERROR, "  FEHLER %s", name);                   \
      s_fails++;                                                           \
    }                                                                      \
  } while (0)

int cycle_selftest_run(void) {
  s_fails = 0;
  const int32_t A = 20000;   // beliebiger Ankertag

  APP_LOG(APP_LOG_LEVEL_INFO, "== 8 Wochen an, 2 aus ==");
  {
    // Periode 70 Tage: Tag 0..55 Einnahme, Tag 56..69 Pause.
    CycleState s = cycle_state(A, A, 8, 2);
    EQ("Tag 0: Phase", s.phase, CyclePhaseOn);
    EQ("Tag 0: Woche", s.week, 1);
    EQ("Tag 0: von", s.of_weeks, 8);
    EQ("Tag 0: Tage bis Wechsel", s.days_left, 56);

    s = cycle_state(A, A + 6, 8, 2);
    EQ("Tag 6 noch Woche 1", s.week, 1);
    s = cycle_state(A, A + 7, 8, 2);
    EQ("Tag 7 ist Woche 2", s.week, 2);

    s = cycle_state(A, A + 55, 8, 2);
    EQ("Tag 55: letzter Einnahmetag, Phase", s.phase, CyclePhaseOn);
    EQ("Tag 55: Woche 8", s.week, 8);
    EQ("Tag 55: noch 1 Tag", s.days_left, 1);

    s = cycle_state(A, A + 56, 8, 2);
    EQ("Tag 56: Pause beginnt", s.phase, CyclePhaseOff);
    EQ("Tag 56: Pausenwoche 1", s.week, 1);
    EQ("Tag 56: von 2", s.of_weeks, 2);
    EQ("Tag 56: noch 14 Tage", s.days_left, 14);

    s = cycle_state(A, A + 69, 8, 2);
    EQ("Tag 69: letzter Pausentag", s.phase, CyclePhaseOff);
    EQ("Tag 69: Pausenwoche 2", s.week, 2);
    EQ("Tag 69: noch 1 Tag", s.days_left, 1);

    s = cycle_state(A, A + 70, 8, 2);
    EQ("Tag 70: neue Runde", s.phase, CyclePhaseOn);
    EQ("Tag 70: wieder Woche 1", s.week, 1);
  }

  APP_LOG(APP_LOG_LEVEL_INFO, "== Nach vielen Runden ==");
  {
    // Fünf volle Perioden weiter muss es aussehen wie am Anfang.
    CycleState s = cycle_state(A, A + 5 * 70, 8, 2);
    EQ("Phase wie an Tag 0", s.phase, CyclePhaseOn);
    EQ("Woche wie an Tag 0", s.week, 1);
    EQ("Tage bis Wechsel wie an Tag 0", s.days_left, 56);

    s = cycle_state(A, A + 5 * 70 + 60, 8, 2);
    EQ("mitten in der 6. Pause: Phase", s.phase, CyclePhaseOff);
    EQ("mitten in der 6. Pause: Woche", s.week, 1);
  }

  APP_LOG(APP_LOG_LEVEL_INFO, "== Ohne Pause (dauerhaft) ==");
  {
    CycleState s = cycle_state(A, A + 500, 4, 0);
    EQ("immer Einnahme", s.phase, CyclePhaseOn);
    EQ("kein Wechsel in Sicht", s.days_left, 0);
    OK("Wochenzaehler laeuft mit", s.week == 500 / 7 + 1);
    OK("dauerhaft heisst heute aktiv", cycle_active_today(A, A + 500, 4, 0));
  }

  APP_LOG(APP_LOG_LEVEL_INFO, "== Kurze Zyklen ==");
  {
    // 1 Woche an, 1 aus: Periode 14 Tage.
    OK("Tag 0 aktiv", cycle_active_today(A, A, 1, 1));
    OK("Tag 6 aktiv", cycle_active_today(A, A + 6, 1, 1));
    OK("Tag 7 pausiert", !cycle_active_today(A, A + 7, 1, 1));
    OK("Tag 13 pausiert", !cycle_active_today(A, A + 13, 1, 1));
    OK("Tag 14 wieder aktiv", cycle_active_today(A, A + 14, 1, 1));
  }

  APP_LOG(APP_LOG_LEVEL_INFO, "== Raender ==");
  {
    // Vor dem Anker: der Zyklus hat noch nicht begonnen. Rueckwaerts zu
    // rechnen ergaebe eine Phase, die es nie gab.
    CycleState s = cycle_state(A, A - 30, 8, 2);
    EQ("vor dem Anker: Phase", s.phase, CyclePhaseOn);
    EQ("vor dem Anker: Woche 1", s.week, 1);

    // Unsinnige Eingaben duerfen nicht in eine Division durch Null laufen.
    s = cycle_state(A, A + 10, 0, 2);
    EQ("weeks_on 0 wird zu 1: Phase", s.phase, CyclePhaseOff);
    OK("weeks_on 0 stuerzt nicht ab", s.of_weeks == 2);
    s = cycle_state(A, A + 10, 8, -3);
    EQ("negative Pause gilt als keine", s.phase, CyclePhaseOn);
    EQ("und damit kein Wechsel", s.days_left, 0);
  }

  APP_LOG(APP_LOG_LEVEL_INFO, "== Raster: alle X Tage ==");
  {
    // every == 1 heisst jeden Tag - und zwar wirklich jeden, auch weit weg
    // vom Anker.
    OK("alle 1 Tage: Ankertag", cycle_day_hits(A, A, 1));
    OK("alle 1 Tage: Tag danach", cycle_day_hits(A, A + 1, 1));
    OK("alle 1 Tage: Tag 999", cycle_day_hits(A, A + 999, 1));

    // every == 3: Anker, +3, +6 treffen; +1, +2, +4 nicht.
    OK("alle 3 Tage: Anker trifft", cycle_day_hits(A, A, 3));
    OK("alle 3 Tage: +1 trifft nicht", !cycle_day_hits(A, A + 1, 3));
    OK("alle 3 Tage: +2 trifft nicht", !cycle_day_hits(A, A + 2, 3));
    OK("alle 3 Tage: +3 trifft", cycle_day_hits(A, A + 3, 3));
    OK("alle 3 Tage: +4 trifft nicht", !cycle_day_hits(A, A + 4, 3));
    OK("alle 3 Tage: +6 trifft", cycle_day_hits(A, A + 6, 3));
    OK("alle 3 Tage: +30 trifft", cycle_day_hits(A, A + 30, 3));
    OK("alle 3 Tage: +31 trifft nicht", !cycle_day_hits(A, A + 31, 3));

    // Gerade Raster: +14 bei alle 2 Tage trifft, +15 nicht.
    OK("alle 2 Tage: +14 trifft", cycle_day_hits(A, A + 14, 2));
    OK("alle 2 Tage: +15 trifft nicht", !cycle_day_hits(A, A + 15, 2));

    // Vor dem Anker gilt der Anker. Ein Modulo auf einer negativen Differenz
    // gaebe je nach Umsetzung ein anderes Vorzeichen - und das Raster
    // verschoebe sich um einen Tag.
    OK("vor dem Anker trifft", cycle_day_hits(A, A - 5, 3));
    OK("weit vor dem Anker trifft", cycle_day_hits(A, A - 100, 7));

    // Unsinn darf nicht durch Null teilen.
    OK("every 0 gilt als taeglich", cycle_day_hits(A, A + 1, 0));
    OK("every negativ gilt als taeglich", cycle_day_hits(A, A + 1, -4));
  }

  APP_LOG(APP_LOG_LEVEL_INFO, "== SELBSTTEST FERTIG, Fehler: %d ==", s_fails);
  return s_fails;
}
#else
typedef int cycle_selftest_not_built;
#endif
