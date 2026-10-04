#include "plan.h"

#define PERSIST_PLAN   1
#define PERSIST_DAY    2
#define PERSIST_TAKEN  3
// DIE HAKENZEITEN LIEGEN IN FACH 8. In 0.14.0 lagen sie in Fach 4 - das
// gehoert aber prefs.c (Animation an/aus). Beide schrieben hinein: die
// Animation las die erste Hakenzeit als Schalter (kein Haken auf Platz 1 =
// Animation aus, schon gleich nach der Installation, und die Uhr meldete das
// dem Telefon - im Emulator nachgestellt), und ein Umschalten der
// Animation ersetzte die sechs Zeiten durch eine Zahl. 5 bis 7 belegt
// remind.c. Was 0.14.0 in Fach 4 schrieb (24 Byte), holt plan_init herueber.
#define PERSIST_TAKEN_AT 8
#define PERSIST_TAKEN_AT_014 4

static PlanItem s_items[SC_MAX_ITEMS];
static uint8_t s_taken;      //< ein Bit je Eintrag
static int32_t s_taken_day;  //< für welchen Tag die Bits gelten
// Wann jeder Haken gesetzt wurde (Sekunden seit 1970), 0 ohne Haken. Im
// Persist neben den Bits: die App geht nach dem Abhaken zu, und das Telefon
// hoert womoeglich erst beim naechsten Start davon.
static uint32_t s_taken_at[SC_MAX_ITEMS];

static void prv_save_taken_at(void) {
  persist_write_data(PERSIST_TAKEN_AT, s_taken_at, sizeof(s_taken_at));
}

// 01.01.2025 in Tagen seit der Epoche: eine Uhr davor hat ihre Zeit noch nicht.
#define SC_TAG_2025 20089

// So weit darf die Uhr vom Telefon abweichen, damit ihre Zeit als bestaetigt
// gilt (siehe plan_uhr_bestaetigt). Stellt das Telefon die Uhr, liegen beide
// Sekunden auseinander; eine Uhr, die nach einem Neustart auf einer alten
// Zeit steht, Minuten bis Stunden.
#define SC_UHR_ABWEICHUNG_MAX 300

// Der Tag einer Zeit, als Tage seit Epoche aus der Ortszeit: Sekunden seit
// Mitternacht abziehen und dann in Tage teilen. Ohne mktime, wie in den
// Schwesterapps.
static int32_t prv_tag(time_t t) {
  struct tm *lt = localtime(&t);
  const time_t midnight = t - (lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec);
  return (int32_t)(midnight / 86400);
}

int32_t plan_today(void) {
  return prv_tag(time(NULL));
}

// Die Abhak-Vermerke gelten immer nur für den laufenden Tag. Bei einem
// Tageswechsel fallen sie weg - sonst stünde morgen alles schon als erledigt da.
//
// NUR VORWAERTS. Springt die Uhr zurück - etwa wenn das Telefon nach einer
// Unterbrechung oder einem Neustart die Zeitzone neu setzt -, gehören die
// Haken zum Tag, an dem man wirklich ist. Sie wegzuwerfen hiess: Abgehaktes
// stand nach dem Verbinden wieder offen da.
//
// UND DEN TAG NICHT ZURUECKSCHREIBEN. Nach einem Firmware-Update oder
// Neustart steht die Uhr kurz auf einer alten Zeit, bis das Telefon sie
// stellt. Frueher wurde dieser alte Tag gemerkt - und die richtige Zeit danach
// sah aus wie ein neuer Tag: alle Haken weg (01.10.2026, nach Firmware b18).
// Der gemerkte Tag bleibt jetzt; erst ein Tag NACH ihm macht die Haken leer -
// auch fuer einen Haken, den man setzt, WAEHREND die Uhr so zurueckliegt.
//
// DIE UHR ALLEIN KANN NICHT ENTSCHEIDEN, WELCHE ZEIT FALSCH WAR. Steht sie
// hinter dem gemerkten Tag, liegt sie entweder jetzt zurueck (Neustart, s. o.)
// oder sie ging vorher vor und ist jetzt richtig (Audit M10: dann hingen die
// Haken des echten Tages am vorausgeeilten Tag und standen am echten Folgetag
// noch da, als waere genommen worden). Fuer die Uhr sehen beide Faelle gleich
// aus. Darum fragt sie in diesem Zustand das Telefon nach seiner Zeit
// (plan_uhr_fraglich, phone.c) und gibt den gemerkten Tag erst auf, wenn das
// Telefon ihre Zeit bestaetigt (plan_uhr_bestaetigt).
// NICHT nach der Zeit jedes Hakens: ein Haken, den man setzt, waehrend die Uhr
// nach einem Neustart auf gestern steht, traegt die Zeit von gestern - er fiele
// beim Stellen der Uhr weg, obwohl er heute gesetzt wurde.
static void prv_roll_day(void) {
  const int32_t today = plan_today();
  if (s_taken_day == today) return;
  if (today < s_taken_day) {
    // Liegt der gemerkte Tag weit voraus und geht die Uhr plausibel, war der
    // gemerkte Tag der falsche (die Uhr stand einmal in der Zukunft): dann
    // gilt heute, die Haken bleiben. Sonst - eine Uhr vor 2025 oder ein
    // Sprung von Stunden oder ein bis zwei Tagen - bleibt alles, wie es ist.
    if (today >= SC_TAG_2025 && s_taken_day - today > 2) {
      s_taken_day = today;
      persist_write_int(PERSIST_DAY, (int)today);
    }
    return;
  }
  s_taken_day = today;
  s_taken = 0;
  memset(s_taken_at, 0, sizeof(s_taken_at));
  persist_write_int(PERSIST_DAY, (int)today);
  persist_write_int(PERSIST_TAKEN, 0);
  prv_save_taken_at();
}

bool plan_uhr_fraglich(void) {
  const int32_t today = plan_today();
  return today < s_taken_day && today >= SC_TAG_2025;
}

bool plan_uhr_bestaetigt(uint32_t telefon) {
  const int32_t abweichung = (int32_t)(time(NULL) - (time_t)telefon);
  if (abweichung > SC_UHR_ABWEICHUNG_MAX || abweichung < -SC_UHR_ABWEICHUNG_MAX) {
    // Die Uhr steht (noch) falsch - genau der Neustartfall. Nichts aendern.
    APP_LOG(APP_LOG_LEVEL_INFO, "Telefonzeit weicht %d s ab - Tag bleibt", (int)abweichung);
    return false;
  }
  const int32_t today = plan_today();
  // Kurz vor Mitternacht koennen Uhr und Telefon auf verschiedenen Tagen
  // stehen, obwohl sie nur Sekunden trennen. Dann lieber nichts.
  if (prv_tag((time_t)telefon) != today) return false;
  if (!(today < s_taken_day && today >= SC_TAG_2025)) return false;
  APP_LOG(APP_LOG_LEVEL_INFO, "Uhr vom Telefon bestaetigt: Tag %d statt %d, Haken bleiben",
          (int)today, (int)s_taken_day);
  s_taken_day = today;
  persist_write_int(PERSIST_DAY, (int)today);
  return true;
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

static bool prv_parse(const uint8_t *data, uint16_t len, PlanItem *fresh);

void plan_init(void) {
  // DER GESPEICHERTE PLAN IST KEIN NEUER. Frueher lief er durch
  // plan_set_from_bytes - gegen ein noch leeres s_items war dort jeder Name
  // "anders", alle Haken fielen weg und die 0 landete im Persist, bevor sie
  // gelesen wurde: jeder App-Start machte den Tag wieder offen.
  if (persist_exists(PERSIST_PLAN)) {
    uint8_t buf[SC_MAX_ITEMS * SC_ITEM_BYTES];
    const int n = persist_read_data(PERSIST_PLAN, buf, sizeof(buf));
    PlanItem gespeichert[SC_MAX_ITEMS];
    if (n > 0 && prv_parse(buf, (uint16_t)n, gespeichert)) {
      memcpy(s_items, gespeichert, sizeof(s_items));
    }
  }
#ifdef SC_FAKE_PLAN
  prv_fake_plan();
#endif
  s_taken_day = persist_exists(PERSIST_DAY) ? (int32_t)persist_read_int(PERSIST_DAY) : 0;
  s_taken = persist_exists(PERSIST_TAKEN) ? (uint8_t)persist_read_int(PERSIST_TAKEN) : 0;
  // Fehlt der Wert (Haken von vor 0.14.0), bleiben die Zeiten 0.
  memset(s_taken_at, 0, sizeof(s_taken_at));
  if (persist_exists(PERSIST_TAKEN_AT)) {
    persist_read_data(PERSIST_TAKEN_AT, s_taken_at, sizeof(s_taken_at));
  } else if (persist_get_size(PERSIST_TAKEN_AT_014) == (int)sizeof(s_taken_at)) {
    // Die Zeiten von 0.14.0 an ihren Platz; Fach 4 gehoert wieder prefs.c,
    // das bei dieser Groesse schon die Voreinstellung genommen hat.
    persist_read_data(PERSIST_TAKEN_AT_014, s_taken_at, sizeof(s_taken_at));
    prv_save_taken_at();
    persist_delete(PERSIST_TAKEN_AT_014);
  }
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

// Die Bytes der Leitung (oder des Persists) in Eintraege; false bei Unsinn.
static bool prv_parse(const uint8_t *data, uint16_t len, PlanItem *fresh) {
  if (!data) return false;
  memset(fresh, 0, sizeof(PlanItem) * SC_MAX_ITEMS);

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
  return true;
}

bool plan_set_from_bytes(const uint8_t *data, uint16_t len) {
  PlanItem fresh[SC_MAX_ITEMS];
  if (!prv_parse(data, len, fresh)) return false;
  if (memcmp(fresh, s_items, sizeof(s_items)) == 0) return false;

  // Die Haken hängen am PLATZ, nicht am Präparat: s_taken ist eine Bitmaske
  // über die Indizes. Steht auf einem Platz plötzlich ein anderer Name, gilt
  // sein Haken nicht mehr - sonst stünde das neue Präparat ungefragt als
  // genommen da. Das ist schlimmer als ein fehlender Haken: es behauptet eine
  // Einnahme, die nie stattgefunden hat.
  const uint8_t vorher = s_taken;
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    if (strncmp(fresh[i].name, s_items[i].name, SC_NAME_LEN) != 0) {
      s_taken &= (uint8_t)~(1u << i);
      s_taken_at[i] = 0;
    }
  }
  if (s_taken != vorher) {
    persist_write_int(PERSIST_TAKEN, s_taken);
    prv_save_taken_at();
  }

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
  const uint8_t bit = (uint8_t)(1u << index);
  if (taken) {
    // Ein Haken, der schon steht, behaelt seine Zeit: die Erinnerung hakt die
    // ganze Runde ab, und die Einnahme davor war die echte.
    if (!(s_taken & bit)) s_taken_at[index] = (uint32_t)time(NULL);
    s_taken |= bit;
  } else {
    s_taken &= (uint8_t)~bit;
    s_taken_at[index] = 0;
  }
  persist_write_int(PERSIST_TAKEN, s_taken);
  prv_save_taken_at();
}

uint32_t plan_taken_at(int index) {
  if (!plan_taken(index)) return 0;
  return s_taken_at[index];
}

uint16_t plan_taken_at_to_bytes(uint8_t *out) {
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    const uint32_t t = plan_taken_at(i);
    out[i * 4 + 0] = (uint8_t)(t & 0xFF);
    out[i * 4 + 1] = (uint8_t)((t >> 8) & 0xFF);
    out[i * 4 + 2] = (uint8_t)((t >> 16) & 0xFF);
    out[i * 4 + 3] = (uint8_t)((t >> 24) & 0xFF);
  }
  return SC_TAKEN_AT_BYTES;
}

bool plan_untake(uint32_t mask, const uint8_t *at, uint16_t len) {
  prv_roll_day();
  bool changed = false;
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    if (!(mask & (1u << i)) || !(s_taken & (1u << i))) continue;
    // Ohne Zeit fuer diesen Platz laesst sich nicht sagen, ob es noch
    // derselbe Haken ist - dann bleibt er.
    if (!at || len < (i + 1) * 4) continue;
    const uint8_t *p = at + i * 4;
    const uint32_t gemeint = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                             ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    if (gemeint != s_taken_at[i]) continue;
    s_taken &= (uint8_t)~(1u << i);
    s_taken_at[i] = 0;
    changed = true;
  }
  if (changed) {
    persist_write_int(PERSIST_TAKEN, s_taken);
    prv_save_taken_at();
  }
  return changed;
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

