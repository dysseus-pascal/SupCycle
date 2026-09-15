#pragma once
#include <pebble.h>
#include "cycle.h"

// Der Plan: welche Präparate, wann, und mit welchem Zyklus.
//
// Eingetragen wird alles auf der Konfigseite der Telefon-App. Die Uhr bekommt
// den fertigen Plan als einen einzigen Datenblock geschickt und merkt ihn
// sich — sie muss ihn nie selbst erfragen, und ohne Telefon läuft sie mit dem
// zuletzt empfangenen weiter.

#define SC_MAX_ITEMS 6
#define SC_NAME_LEN  16   //< Bytes, nicht Zeichen. Umlaute zählen doppelt.

// Ein Eintrag auf der Leitung UND im Speicher: 25 Byte, feste Reihenfolge.
// Sechs davon sind 150 Byte und passen damit in einen Persist-Wert (256).
#define SC_ITEM_BYTES 25

typedef enum {
  PlanUnused = 0,
  PlanDaily,      //< jeden Tag
  PlanCyclic,     //< Wochen an, Wochen aus
} PlanMode;

typedef struct {
  char name[SC_NAME_LEN];
  uint8_t hour;
  uint8_t minute;
  uint8_t mode;         //< PlanMode
  uint8_t weeks_on;
  uint8_t weeks_off;
  int32_t anchor_day;   //< Tage seit Epoche, an denen Woche 1 begann
} PlanItem;

void plan_init(void);

// Plan aus einem empfangenen Datenblock übernehmen und persistieren.
// Rückgabe: true, wenn er sich geändert hat.
bool plan_set_from_bytes(const uint8_t *data, uint16_t len);

int plan_count(void);                    //< benutzte Einträge, 0..SC_MAX_ITEMS
const PlanItem *plan_item(int index);

// Heutiger Tag als Tage seit Epoche, aus der ORTSZEIT. Gerechnet wird überall
// in ganzen Tagen: Sommerzeit verschiebt einen Tag um eine Stunde, und über
// acht Wochen summiert sich das zu einem Fehler von einem Tag.
int32_t plan_today(void);

// Steht dieses Präparat heute an? Dauerhafte immer, zyklische je nach Phase.
bool plan_due_today(int index);

// Dasselbe für einen beliebigen Tag (Tage seit Epoche). Die Weckplanung
// braucht das: sie stellt auch Wecker für morgen, und morgen kann ein Zyklus
// schon in der Pause sein.
bool plan_due_on(int index, int32_t day);

// Zyklusstand für die Anzeige. Bei dauerhaften Einträgen ist phase immer
// CyclePhaseOn und days_left 0.
CycleState plan_cycle(int index);

// Abgehakt? Der Vermerk gilt für EINEN Tag; um Mitternacht fällt er weg.
bool plan_taken(int index);
void plan_set_taken(int index, bool taken);

// Wie viele der heute fälligen sind noch offen.
int plan_open_today(void);

// Nächster heute fälliger und noch offener Eintrag, -1 wenn keiner.
int plan_next_open(void);
