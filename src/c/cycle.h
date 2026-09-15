#pragma once
#include <stdbool.h>
#include <stdint.h>

// Wo in seinem Zyklus steht ein Präparat gerade? Reine Rechnung, KEIN pebble.h.
//
// Diese Datei hängt bewusst an nichts: sie lässt sich im Emulator gegen vorab
// bestimmte Erwartungen prüfen (src/c/cycle_selftest.c). Das ist hier kein
// Selbstzweck — eine Zyklusrechnung, die um eine Woche danebenliegt, fällt im
// Betrieb erst nach Wochen auf, und dann hat man schon falsch dosiert.
//
// GERECHNET WIRD IN TAGEN, nicht in Sekunden: Sommerzeit verschiebt einen Tag
// um eine Stunde, und über acht Wochen summiert sich das. Mit ganzen Tagen
// seit einem Ankertag kann das nicht passieren.

typedef enum {
  CyclePhaseOff = 0,   //< Pause
  CyclePhaseOn,        //< Einnahme
} CyclePhase;

typedef struct {
  CyclePhase phase;
  int week;        //< laufende Woche INNERHALB der Phase, 1-basiert
  int of_weeks;    //< wie viele Wochen diese Phase dauert
  int days_left;   //< Tage bis zum Wechsel, mindestens 1 am letzten Tag
} CycleState;

// Zustand eines zyklischen Präparats.
//
//   anchor_day  Tag, an dem Woche 1 der Einnahme begann (Tage seit Epoche)
//   today       heutiger Tag (Tage seit Epoche)
//   weeks_on    Wochen Einnahme, mindestens 1
//   weeks_off   Wochen Pause, 0 = nie Pause (dann immer CyclePhaseOn)
//
// Liegt `today` vor dem Anker, gilt der Anker - ein Zyklus, der erst morgen
// beginnt, soll heute nicht rückwärts gerechnet werden.
CycleState cycle_state(int32_t anchor_day, int32_t today, int weeks_on, int weeks_off);

// Nimmt man das Präparat heute?
bool cycle_active_today(int32_t anchor_day, int32_t today, int weeks_on, int weeks_off);
