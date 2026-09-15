#include "plan.h"

#define PERSIST_PLAN   1
#define PERSIST_DAY    2
#define PERSIST_TAKEN  3

static PlanItem s_items[SC_MAX_ITEMS];
static uint8_t s_taken;      //< ein Bit je Eintrag
static int32_t s_taken_day;  //< für welchen Tag die Bits gelten

int32_t plan_today(void) {
  const time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  // Sekunden seit Mitternacht abziehen und dann in Tage teilen. Ohne mktime,
  // wie in den Schwesterapps.
  const time_t midnight = now - (lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec);
  return (int32_t)(midnight / 86400);
}

// Die Abhak-Vermerke gelten immer nur für den laufenden Tag. Bei einem
// Tageswechsel fallen sie weg - sonst stünde morgen alles schon als erledigt da.
static void prv_roll_day(void) {
  const int32_t today = plan_today();
  if (s_taken_day == today) return;
  s_taken_day = today;
  s_taken = 0;
  persist_write_int(PERSIST_DAY, (int)today);
  persist_write_int(PERSIST_TAKEN, 0);
}

#ifdef SC_FAKE_PLAN
// Nur im Pruefbau: ein Beispielplan, damit sich die Ansichten im Emulator
// ansehen lassen. Dort gibt es keine Konfigseite und damit keinen Plan - ohne
// das bliebe der Schirm auf "Noch kein Plan" stehen.
//
// Zwei dauerhafte und zwei zyklische, die Zyklen bewusst in verschiedenen
// Phasen: eines mitten in der Einnahme, eines in der Pause. Nur so sieht man
// beim Fotografieren, ob beide Faelle richtig dargestellt werden.
static void prv_fake_plan(void) {
  const int32_t today = plan_today();
  struct { const char *name; int h; int m; int mode; int on; int off; int since_days; }
  demo[] = {
    { "Multivitamin", 8, 0,  PlanDaily,  0, 0,  0 },
    { "Kreatin",      8, 0,  PlanDaily,  0, 0,  0 },
    { "Black Maca",  12, 30, PlanCyclic, 8, 2, 16 },   // Woche 3 der Einnahme
    { "Ashwagandha", 20, 0,  PlanCyclic, 6, 2, 45 },   // in der Pause
  };
  memset(s_items, 0, sizeof(s_items));
  for (unsigned i = 0; i < sizeof(demo) / sizeof(demo[0]); i++) {
    strncpy(s_items[i].name, demo[i].name, SC_NAME_LEN - 1);
    s_items[i].hour = (uint8_t)demo[i].h;
    s_items[i].minute = (uint8_t)demo[i].m;
    s_items[i].mode = (uint8_t)demo[i].mode;
    s_items[i].weeks_on = (uint8_t)demo[i].on;
    s_items[i].weeks_off = (uint8_t)demo[i].off;
    s_items[i].anchor_day = today - demo[i].since_days;
  }
}
#endif

void plan_init(void) {
  if (persist_exists(PERSIST_PLAN)) {
    uint8_t buf[SC_MAX_ITEMS * SC_ITEM_BYTES];
    const int n = persist_read_data(PERSIST_PLAN, buf, sizeof(buf));
    if (n > 0) plan_set_from_bytes(buf, (uint16_t)n);
  }
#ifdef SC_FAKE_PLAN
  prv_fake_plan();
#endif
  s_taken_day = persist_exists(PERSIST_DAY) ? (int32_t)persist_read_int(PERSIST_DAY) : 0;
  s_taken = persist_exists(PERSIST_TAKEN) ? (uint8_t)persist_read_int(PERSIST_TAKEN) : 0;
  prv_roll_day();
}

// Ein Eintrag auf der Leitung: 16 Byte Name, dann Stunde, Minute, Modus,
// Wochen an, Wochen aus, dann 4 Byte Ankertag (little endian).
static void prv_read_item(const uint8_t *p, PlanItem *out) {
  memcpy(out->name, p, SC_NAME_LEN);
  out->name[SC_NAME_LEN - 1] = 0;   // was auch kommt: die Zeichenkette endet
  out->hour = p[16];
  out->minute = p[17];
  out->mode = p[18];
  out->weeks_on = p[19];
  out->weeks_off = p[20];
  out->anchor_day = (int32_t)((uint32_t)p[21] | ((uint32_t)p[22] << 8) |
                              ((uint32_t)p[23] << 16) | ((uint32_t)p[24] << 24));

  // Unsinn abfangen, statt ihn anzuzeigen. Die Telefonseite prüft schon, aber
  // ein verdorbener Persist-Wert käme hier sonst ungebremst durch.
  if (out->mode > PlanCyclic) out->mode = PlanUnused;
  if (out->hour > 23) out->hour = 8;
  if (out->minute > 59) out->minute = 0;
  if (out->name[0] == 0) out->mode = PlanUnused;
  if (out->mode == PlanCyclic && out->weeks_on < 1) out->weeks_on = 1;
}

bool plan_set_from_bytes(const uint8_t *data, uint16_t len) {
  if (!data) return false;
  PlanItem fresh[SC_MAX_ITEMS];
  memset(fresh, 0, sizeof(fresh));

  const int n = len / SC_ITEM_BYTES;
  for (int i = 0; i < n && i < SC_MAX_ITEMS; i++) {
    prv_read_item(data + i * SC_ITEM_BYTES, &fresh[i]);
  }

  if (memcmp(fresh, s_items, sizeof(s_items)) == 0) return false;
  memcpy(s_items, fresh, sizeof(s_items));
  persist_write_data(PERSIST_PLAN, data, len < sizeof(s_items) ? len : (uint16_t)sizeof(s_items));
  APP_LOG(APP_LOG_LEVEL_INFO, "Plan uebernommen: %d Eintraege", plan_count());
  return true;
}

int plan_count(void) {
  int n = 0;
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    if (s_items[i].mode != PlanUnused) n++;
  }
  return n;
}

const PlanItem *plan_item(int index) {
  if (index < 0 || index >= SC_MAX_ITEMS) return NULL;
  return &s_items[index];
}

CycleState plan_cycle(int index) {
  CycleState s = { CyclePhaseOn, 1, 0, 0 };
  const PlanItem *it = plan_item(index);
  if (!it || it->mode != PlanCyclic) return s;
  return cycle_state(it->anchor_day, plan_today(), it->weeks_on, it->weeks_off);
}

bool plan_due_on(int index, int32_t day) {
  const PlanItem *it = plan_item(index);
  if (!it) return false;
  switch (it->mode) {
    case PlanDaily:  return true;
    case PlanCyclic: return cycle_active_today(it->anchor_day, day,
                                               it->weeks_on, it->weeks_off);
    default:         return false;
  }
}

bool plan_due_today(int index) {
  return plan_due_on(index, plan_today());
}

bool plan_taken(int index) {
  if (index < 0 || index >= SC_MAX_ITEMS) return false;
  prv_roll_day();
  return (s_taken & (1u << index)) != 0;
}

void plan_set_taken(int index, bool taken) {
  if (index < 0 || index >= SC_MAX_ITEMS) return;
  prv_roll_day();
  if (taken) s_taken |= (uint8_t)(1u << index);
  else s_taken &= (uint8_t)~(1u << index);
  persist_write_int(PERSIST_TAKEN, s_taken);
}

int plan_open_today(void) {
  int n = 0;
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    if (plan_due_today(i) && !plan_taken(i)) n++;
  }
  return n;
}

int plan_next_open(void) {
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    if (plan_due_today(i) && !plan_taken(i)) return i;
  }
  return -1;
}
