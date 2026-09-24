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
// Phasen: eines mitten in der Einnahme, eines in der Pause. Eines laeuft
// zusaetzlich auf einem Zweitageraster. Nur so sieht man beim Fotografieren,
// ob alle Faelle richtig dargestellt werden.
static void prv_fake_plan(void) {
  const int32_t today = plan_today();
  struct { const char *name; int h; int m; int used; int every; int on; int off;
           int since_days; }
  demo[] = {
    //  Name          Std Min  benutzt every on off  Anker
    { "Multivitamin", 8, 0,  1, 1, 0, 0,  0 },   // täglich, unbegrenzt
    { "Kreatin",      8, 0,  1, 1, 0, 0,  0 },   // täglich, unbegrenzt
    { "Black Maca",  12, 30, 1, 1, 8, 2, 16 },   // Woche 3 der Einnahme
    { "Ashwagandha", 20, 0,  1, 2, 6, 2, 45 },   // in der Pause, alle 2 Tage
  };
  memset(s_items, 0, sizeof(s_items));
  for (unsigned i = 0; i < sizeof(demo) / sizeof(demo[0]); i++) {
    strncpy(s_items[i].name, demo[i].name, SC_NAME_LEN - 1);
    s_items[i].hour = (uint8_t)demo[i].h;
    s_items[i].minute = (uint8_t)demo[i].m;
    s_items[i].used = (uint8_t)demo[i].used;
    s_items[i].every = (uint8_t)demo[i].every;
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
// Unsinn abfangen, statt ihn anzuzeigen. Die Telefonseite prüft schon, aber
// ein verdorbener Persist-Wert käme hier sonst ungebremst durch.
static void prv_sanitize(PlanItem *out) {
  if (out->hour > 23) out->hour = 8;
  if (out->minute > 59) out->minute = 0;
  if (out->name[0] == 0) out->used = 0;
  if (out->every < 1) out->every = 1;
  // Ein Raster, das länger ist als der Zyklus selbst, träfe womöglich nie
  // einen Einnahmetag. Dann ist die Eingabe falsch, nicht der Plan.
  if (out->every > 30) out->every = 30;
  if (out->weeks_on > 52) out->weeks_on = 52;
  if (out->weeks_off > 52) out->weeks_off = 52;
}

static void prv_read_item(const uint8_t *p, PlanItem *out) {
  memcpy(out->name, p, SC_NAME_LEN);
  out->name[SC_NAME_LEN - 1] = 0;   // was auch kommt: die Zeichenkette endet
  out->hour = p[16];
  out->minute = p[17];
  out->used = p[18] ? 1 : 0;
  out->every = p[19];
  out->weeks_on = p[20];
  out->weeks_off = p[21];
  out->anchor_day = (int32_t)((uint32_t)p[22] | ((uint32_t)p[23] << 8) |
                              ((uint32_t)p[24] << 16) | ((uint32_t)p[25] << 24));
  prv_sanitize(out);
}

// Ein Eintrag im alten 25-Byte-Format. Aus mode werden benutzt und die
// Zyklusfelder: täglich hiess dort "keine Pause", also weeks_on = 0.
static void prv_read_item_v1(const uint8_t *p, PlanItem *out) {
  memcpy(out->name, p, SC_NAME_LEN);
  out->name[SC_NAME_LEN - 1] = 0;
  out->hour = p[16];
  out->minute = p[17];
  const uint8_t mode = p[18];
  out->used = (mode == 1 || mode == 2) ? 1 : 0;
  out->every = 1;
  out->weeks_on = (mode == 2) ? p[19] : 0;
  out->weeks_off = (mode == 2) ? p[20] : 0;
  out->anchor_day = (int32_t)((uint32_t)p[21] | ((uint32_t)p[22] << 8) |
                              ((uint32_t)p[23] << 16) | ((uint32_t)p[24] << 24));
  prv_sanitize(out);
}

bool plan_set_from_bytes(const uint8_t *data, uint16_t len) {
  if (!data) return false;
  PlanItem fresh[SC_MAX_ITEMS];
  memset(fresh, 0, sizeof(fresh));

  // Welches Format? 156 (6x26) ist nicht durch 25 teilbar und 150 (6x25) nicht
  // durch 26 - die Länge sagt es also eindeutig.
  const uint16_t stride = (len % SC_ITEM_BYTES == 0) ? SC_ITEM_BYTES
                        : (len % SC_ITEM_BYTES_V1 == 0) ? SC_ITEM_BYTES_V1 : 0;
  if (stride == 0) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Plan mit %u Byte passt in kein Format", len);
    return false;
  }
  const int n = len / stride;
  for (int i = 0; i < n && i < SC_MAX_ITEMS; i++) {
    if (stride == SC_ITEM_BYTES) prv_read_item(data + i * stride, &fresh[i]);
    else prv_read_item_v1(data + i * stride, &fresh[i]);
  }

  if (memcmp(fresh, s_items, sizeof(s_items)) == 0) return false;

  // Die Haken hängen am PLATZ, nicht am Präparat: s_taken ist eine Bitmaske
  // über die Indizes. Steht auf einem Platz plötzlich ein anderer Name, gilt
  // sein Haken nicht mehr - sonst stünde das neue Präparat ungefragt als
  // genommen da. Das ist schlimmer als ein fehlender Haken: es behauptet eine
  // Einnahme, die nie stattgefunden hat.
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    if (strncmp(fresh[i].name, s_items[i].name, SC_NAME_LEN) != 0) {
      s_taken &= (uint8_t)~(1u << i);
    }
  }
  persist_write_int(PERSIST_TAKEN, s_taken);

  memcpy(s_items, fresh, sizeof(s_items));
  persist_write_data(PERSIST_PLAN, data, len < sizeof(s_items) ? len : (uint16_t)sizeof(s_items));
  APP_LOG(APP_LOG_LEVEL_INFO, "Plan uebernommen: %d Eintraege", plan_count());
  return true;
}

uint16_t plan_to_bytes(uint8_t *out) {
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    uint8_t *p = out + i * SC_ITEM_BYTES;
    const PlanItem *it = &s_items[i];
    memset(p, 0, SC_ITEM_BYTES);
    memcpy(p, it->name, SC_NAME_LEN);
    p[16] = it->hour;
    p[17] = it->minute;
    p[18] = it->used ? 1 : 0;
    p[19] = it->every;
    p[20] = it->weeks_on;
    p[21] = it->weeks_off;
    const uint32_t a = (uint32_t)it->anchor_day;
    p[22] = (uint8_t)(a & 0xFF);
    p[23] = (uint8_t)((a >> 8) & 0xFF);
    p[24] = (uint8_t)((a >> 16) & 0xFF);
    p[25] = (uint8_t)((a >> 24) & 0xFF);
  }
  return SC_MAX_ITEMS * SC_ITEM_BYTES;
}

int plan_count(void) {
  int n = 0;
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    if (s_items[i].used) n++;
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
  // weeks_on == 0 heisst unbegrenzt: es GIBT keine Phase. Das sagt der
  // Rückgabewert mit of_weeks == 0, und die Anzeige liest es daran ab.
  if (!it || !it->used || it->weeks_on == 0) return s;
  return cycle_state(it->anchor_day, plan_today(), it->weeks_on, it->weeks_off);
}

bool plan_due_on(int index, int32_t day) {
  const PlanItem *it = plan_item(index);
  if (!it || !it->used) return false;
  // Erst das Raster - fällt der Tag nicht darauf, ist die Phase gleichgültig.
  if (!cycle_day_hits(it->anchor_day, day, it->every)) return false;
  if (it->weeks_on == 0) return true;   // unbegrenzt
  return cycle_active_today(it->anchor_day, day, it->weeks_on, it->weeks_off);
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

bool plan_slot_complete(int index) {
  const PlanItem *it = plan_item(index);
  if (!it || !it->used) return true;
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    const PlanItem *o = plan_item(i);
    if (!o || !o->used) continue;
    if (o->hour != it->hour || o->minute != it->minute) continue;
    if (!plan_due_today(i)) continue;
    if (!plan_taken(i)) return false;
  }
  return true;
}

