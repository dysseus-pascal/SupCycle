#include "cycle.h"

CycleState cycle_state(int32_t anchor_day, int32_t today, int weeks_on, int weeks_off) {
  CycleState s;

  if (weeks_on < 1) weeks_on = 1;
  if (weeks_off < 0) weeks_off = 0;

  // OHNE PAUSE IST ES EINE KUR, DIE ENDET: "4 Wochen Einnahme" heisst vier
  // Wochen und dann nicht mehr. Bis 0.15.0 lief der Zaehler einfach weiter
  // ("Woche 72 von 4", Audit N6), und erinnert wurde fuer immer.
  if (weeks_off == 0) {
    const int32_t elapsed = (today > anchor_day) ? (today - anchor_day) : 0;
    const int32_t on_days = (int32_t)weeks_on * 7;
    s.of_weeks = weeks_on;
    if (elapsed < on_days) {
      s.phase = CyclePhaseOn;
      s.week = (int)(elapsed / 7) + 1;
      s.days_left = (int)(on_days - elapsed);
    } else {
      s.phase = CyclePhaseDone;
      s.week = weeks_on;
      s.days_left = 0;    // es kommt kein Wechsel mehr
    }
    return s;
  }

  // Vor dem Anker: der Zyklus hat noch nicht begonnen, also steht er auf
  // seinem ersten Tag. Rückwärts zu rechnen ergäbe eine Phase, die es nie gab.
  const int32_t elapsed = (today > anchor_day) ? (today - anchor_day) : 0;

  const int32_t on_days = (int32_t)weeks_on * 7;
  const int32_t off_days = (int32_t)weeks_off * 7;
  const int32_t period = on_days + off_days;
  const int32_t in_period = elapsed % period;

  if (in_period < on_days) {
    s.phase = CyclePhaseOn;
    s.of_weeks = weeks_on;
    s.week = (int)(in_period / 7) + 1;
    s.days_left = (int)(on_days - in_period);
  } else {
    const int32_t in_off = in_period - on_days;
    s.phase = CyclePhaseOff;
    s.of_weeks = weeks_off;
    s.week = (int)(in_off / 7) + 1;
    s.days_left = (int)(off_days - in_off);
  }
  return s;
}

bool cycle_active_today(int32_t anchor_day, int32_t today, int weeks_on, int weeks_off) {
  return cycle_state(anchor_day, today, weeks_on, weeks_off).phase == CyclePhaseOn;
}

bool cycle_day_hits(int32_t anchor_day, int32_t today, int every) {
  if (every < 1) every = 1;
  if (every == 1) return true;
  // Vor dem Anker gilt der Anker - sonst ergäbe der Modulo auf einer negativen
  // Differenz je nach Umsetzung ein anderes Vorzeichen, und das Raster
  // verschöbe sich.
  int32_t diff = today - anchor_day;
  if (diff < 0) diff = 0;
  return (diff % every) == 0;
}
