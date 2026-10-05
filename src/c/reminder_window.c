#include "reminder_window.h"
#include "theme.h"
#include "phone.h"
#include "plan.h"
#include "prefs.h"
#include "pill_fx.h"
#include "remind.h"
#include "strings.h"


// Das Erinnerungsfenster: ganz in Weiss, oben die Kapsel, daneben die
// Uhrzeit, darunter was ansteht. Rechts die Aktionsleiste.
//
// Es zeigt ALLE Präparate dieser Uhrzeit, die noch offen sind — zwei
// gleichzeitig sind keine zwei Erinnerungen, sondern eine mit zwei Zeilen.
//
//   Mitte   genommen: alles abhaken, Kapsel zerplatzt, App schliesst
//   Unten   später: in SC_SNOOZE_MIN Minuten nochmal - höchstens
//           SC_SNOOZE_MAX mal, danach verfällt die Runde wie beim Wegdrücken
//   Zurück  wegdrücken: die Runde verfällt, die App schliesst. Was liegen
//           blieb, steht auf dem Heute-Schirm und lässt sich dort nachholen.
//
// EINE ERINNERUNG GILT EINER RUNDE. Auch nach einem Aufschub zeigt sie nur
// die Präparate ihrer Uhrzeit - die Mittagsrunde soll nicht den Morgen
// nachtragen, den man bewusst hat liegen lassen.
//
// AUSSER DAS FENSTER STEHT NOCH. Klopft die naechste Runde, waehrend die
// vorige unbeantwortet offen ist, kommt sie dazu: es vibriert neu, und beide
// stehen da. Liegen gelassen hat man die vorige dann nicht bewusst - man hat
// sie nur nicht gesehen.

#define VIBE_PULSES 3
#define VIBE_GAP_MS 20000
// So lange haelt das Fenster die App nach dem Abhaken hoechstens offen, bis
// das Telefon die Meldung bestaetigt hat - wie in Drinktervall.
#define WARTE_TELEFON_MS 5000

static Window *s_window;
static Layer *s_canvas;
static ActionBarLayer *s_bar;
static GBitmap *s_icon_take;
static GBitmap *s_icon_later;
static AppTimer *s_vibe;
static int s_vibes_left;

// Eine Runde: ihre Uhrzeit (Minuten seit Mitternacht, -1 = "was offen ist")
// und ihr Kalendertag (remind_runden_tag): heute, oder gestern fuer einen
// Aufschub, der ueber Mitternacht ging. Beim Erscheinen festgelegt - eine
// Erinnerung von 23:50, die bis nach Mitternacht offen steht, bleibt die
// Runde von gestern.
typedef struct {
  int minute;
  int32_t tag;
} Runde;

// Jede Uhrzeit des Plans, von heute und von gestern.
#define RUNDEN_MAX (2 * SC_MAX_ITEMS)

// DIE RUNDEN DIESES FENSTERS, die neueste zuletzt; meist ist es eine. Bis
// 0.15.0 kehrte reminder_window_push bei offenem Fenster sofort zurueck: die
// naechste Runde vibrierte nicht, war nicht zu sehen, und ihr Wecker war
// verbraucht. Ein einziges liegen gelassenes Fenster schaltete so alle
// spaeteren Runden des Tages stumm. Die neueste gibt die Uhrzeit oben vor,
// und ihr gilt "spaeter".
static Runde s_runden[RUNDEN_MAX];
static int s_runden_zahl;
// Runden, die klopften, waehrend das Fenster nach dem Abhaken nur noch auf
// das Telefon wartet: sie sind danach dran, statt mit der App zu verschwinden.
static Runde s_danach[RUNDEN_MAX];
static int s_danach_zahl;
static bool s_playing;
static bool s_geht;           // abgehakt: wartet nur noch auf das Telefon

static const Runde *prv_neueste(void) {
  return &s_runden[s_runden_zahl - 1];
}

// Die Runde in die Liste. Steht sie schon darin, rueckt sie ans Ende - sie
// ist jetzt die neueste. Ist die Liste voll, faellt die aelteste heraus.
static void prv_merken(Runde *liste, int *zahl, int minute, int32_t tag) {
  for (int k = 0; k < *zahl; k++) {
    if (liste[k].minute != minute || liste[k].tag != tag) continue;
    memmove(&liste[k], &liste[k + 1], (size_t)(*zahl - k - 1) * sizeof(Runde));
    (*zahl)--;
    break;
  }
  if (*zahl >= RUNDEN_MAX) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Runde %d faellt aus dem Fenster", liste[0].minute);
    memmove(&liste[0], &liste[1], (size_t)(*zahl - 1) * sizeof(Runde));
    (*zahl)--;
  }
  liste[*zahl] = (Runde){ .minute = minute, .tag = tag };
  (*zahl)++;
}

// Gehört dieses Präparat zu dieser Runde? Bei -1 (ohne Uhrzeit) zählt alles,
// was an ihrem Tag noch offen ist. Fuer die Runde von gestern zaehlt, was
// gestern anstand - ihre Haken sind um Mitternacht weggefallen, und offen
// ist sie, sonst klopfte ihr Aufschub nicht (remind_schedule). Haken gibt es
// nur fuer heute (plan.h).
static bool prv_in_runde(int i, const Runde *r) {
  if (!plan_due_on(i, r->tag)) return false;
  if (r->tag == plan_today() && plan_taken(i)) return false;
  if (r->minute < 0) return true;
  const PlanItem *it = plan_item(i);
  return it && (it->hour * 60 + it->minute) == r->minute;
}

static int prv_zahl_in(const Runde *r) {
  int n = 0;
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    if (prv_in_runde(i, r)) n++;
  }
  return n;
}

// Gehört es zu einer der Runden dieses Fensters?
static bool prv_in_batch(int i) {
  for (int k = 0; k < s_runden_zahl; k++) {
    if (prv_in_runde(i, &s_runden[k])) return true;
  }
  return false;
}

static int prv_batch_count(void) {
  int n = 0;
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    if (prv_in_batch(i)) n++;
  }
  return n;
}

static void prv_stop_vibes(void) {
  if (s_vibe) {
    app_timer_cancel(s_vibe);
    s_vibe = NULL;
  }
  s_vibes_left = 0;
}

// Dreimal doppelt im Abstand von zwanzig Sekunden, dann Ruhe. Wer nicht
// hinsieht, soll nicht endlos gerüttelt werden.
static void prv_vibe_cb(void *data) {
  s_vibe = NULL;
  if (s_vibes_left <= 0) return;
  vibes_double_pulse();
  if (--s_vibes_left > 0) {
    s_vibe = app_timer_register(VIBE_GAP_MS, prv_vibe_cb, NULL);
  }
}

static void prv_canvas_update(Layer *layer, GContext *ctx) {
  if (s_playing) return;    // während der Animation gehört der Schirm ihr
  const GRect b = layer_get_bounds(layer);
  const bool wide = PBL_DISPLAY_WIDTH >= 180;
  const int16_t margin = PBL_IF_ROUND_ELSE(34, 8);
  const int16_t col_w = b.size.w - ACTION_BAR_WIDTH - margin - 4;

  // Kopf: Kapsel und die Uhrzeit daneben
  // Schmaler als früher: die Kapsel ist jetzt fast doppelt so hoch wie breit,
  // und mit der alten Breite ragte sie in die Trennlinie.
  const int16_t pill_w = wide ? 18 : 16;
  const int16_t head_y = PBL_IF_ROUND_ELSE(46, 26);
  pill_fx_draw_still(ctx, GPoint(margin + pill_w / 2, head_y), pill_w);

  // 12 statt 8 Byte: die Minute liegt zwar immer in 0..1439 ("HH:MM" + NUL),
  // aber gcc kennt den Bereich nicht und warnte bei jedem Bau vor Abschneiden.
  char hhmm[12];
  const int minute = prv_neueste()->minute;
  if (minute >= 0) {
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", minute / 60, minute % 60);
  } else {
    clock_copy_time_string(hhmm, sizeof(hhmm));
  }
  graphics_context_set_text_color(ctx, SC_COLOR_TEXT);
  graphics_draw_text(ctx, hhmm,
                     fonts_get_system_font(wide ? FONT_KEY_LECO_32_BOLD_NUMBERS
                                                : FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM),
                     GRect(margin + pill_w + 8, head_y - (wide ? 22 : 18),
                           col_w - pill_w - 8, wide ? 40 : 34),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // Trennlinie wie im Pin-Detail der Timeline
  int16_t y = head_y + (wide ? 26 : 22);
  graphics_context_set_fill_color(ctx, SC_COLOR_TEXT);
  graphics_fill_rect(ctx, GRect(0, y, b.size.w - ACTION_BAR_WIDTH, 2), 0, GCornerNone);
  y += 10;

  // Was ansteht
  for (int i = 0; i < SC_MAX_ITEMS && y < b.size.h - 6; i++) {
    if (!prv_in_batch(i)) continue;
    const PlanItem *it = plan_item(i);
    graphics_draw_text(ctx, it->name,
                       fonts_get_system_font(wide ? FONT_KEY_GOTHIC_24_BOLD
                                                  : FONT_KEY_GOTHIC_18_BOLD),
                       GRect(margin, y, col_w, wide ? 30 : 24),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    y += wide ? 30 : 24;
  }

  // Der wievielte Aufschub - und ob es der letzte war. Wer es sieht, weiss,
  // dass "spaeter" beim naechsten Mal "heute nicht" heisst.
  const int count = remind_snooze_count(minute);
  if (count > 0) {
    char note[24];
    if (count >= SC_SNOOZE_MAX) snprintf(note, sizeof(note), "%s", S(STR_SNOOZE_LAST));
    else snprintf(note, sizeof(note), S(STR_SNOOZE_FMT), count, SC_SNOOZE_MAX);
    graphics_context_set_text_color(ctx, SC_COLOR_SUB);
    graphics_draw_text(ctx, note, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(margin, b.size.h - PBL_IF_ROUND_ELSE(44, 20), col_w, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

static void prv_close(void) {
  prv_stop_vibes();
  window_stack_pop_all(false);
}

// Das Zeichen fuer "spaeter" nach dem Zaehler der neuesten Runde, dann
// vibrieren - beim Erscheinen und wenn eine Runde dazukommt.
static void prv_klopfen(void) {
  // Kein Aufschub mehr uebrig: dann steht dort auch kein Zeichen dafuer. Ein
  // Zeichen fuer eine Taste, die etwas anderes tut, waere eine Luege.
  if (s_icon_later && remind_snooze_left(prv_neueste()->minute)) {
    action_bar_layer_set_icon(s_bar, BUTTON_ID_DOWN, s_icon_later);
  } else {
    action_bar_layer_clear_icon(s_bar, BUTTON_ID_DOWN);
  }
  // IN DER RUHEZEIT BLEIBT DIE UHR STILL. Der Schirm kommt trotzdem - wer
  // hinsieht, sieht die Erinnerung; wer schlaeft, wird nicht geweckt.
  // quiet_time_is_active kennt Kalender und Schalter, wie in Drinktervall.
  prv_stop_vibes();
  s_vibes_left = quiet_time_is_active() ? 0 : VIBE_PULSES;
  prv_vibe_cb(NULL);
}

// Das Telefon hat den Haken, oder die Frist ist um. Klopfte waehrenddessen
// eine weitere Runde, ist sie jetzt dran - im selben Fenster, neu
// aufgezogen. Sonst geht die App zu.
static void prv_nach_telefon(void) {
  // Nur einmal je Abhaken: ein zweiter Ruf (pill_fx_play ruft sein Ende
  // auch, wenn es nicht zustande kommt) schloesse die neue Runde gleich wieder.
  if (!s_geht) return;
  if (s_danach_zahl > 0) {
    memcpy(s_runden, s_danach, (size_t)s_danach_zahl * sizeof(Runde));
    s_runden_zahl = s_danach_zahl;
    s_danach_zahl = 0;
    if (prv_batch_count() > 0) {
      APP_LOG(APP_LOG_LEVEL_INFO, "Runde %d nach dem Warten", prv_neueste()->minute);
      // Die Animation hat die Leiste weggenommen (prv_take).
      if (s_playing) action_bar_layer_add_to_window(s_bar, s_window);
      s_playing = false;
      s_geht = false;
      prv_klopfen();
      layer_mark_dirty(s_canvas);
      return;
    }
  }
  prv_close();
}

// ERST GEHEN, WENN DAS TELEFON DEN HAKEN HAT. Bis 0.15.0 schloss die App
// gleich nach dem Abschicken (ohne Animation) oder nach 1,4 s (mit), und die
// Uhr verwarf, was noch im Postausgang lag (Audit M3). Jetzt hoechstens
// WARTE_TELEFON_MS laenger; ohne Verbindung geht es sofort.
static void prv_close_nach_telefon(void) {
  phone_when_sent(prv_nach_telefon, WARTE_TELEFON_MS);
}

static void prv_fx_done(void) {
  // s_playing bleibt: bis zum Schliessen gehoert der Schirm der Animation.
  prv_close_nach_telefon();
}

static void prv_take(ClickRecognizerRef recognizer, void *context) {
  if (s_playing || s_geht) return;
  prv_stop_vibes();
  bool heute = false;
  for (int k = 0; k < s_runden_zahl; k++) {
    const Runde *r = &s_runden[k];
    if (r->tag == plan_today()) {
      for (int i = 0; i < SC_MAX_ITEMS; i++) {
        if (prv_in_runde(i, r)) plan_set_taken(i, true);
      }
      heute = true;
    } else {
      // DIE RUNDE VON GESTERN, nach Mitternacht genommen. Ein Haken hiesse
      // hier "heute genommen" - und die Runde gleicher Uhrzeit von heute
      // klopfte am Abend nicht mehr. Lieber fehlt der Vermerk fuer gestern.
      APP_LOG(APP_LOG_LEVEL_INFO, "Runde %d von gestern genommen - kein Haken fuer heute", r->minute);
    }
    // Die eben abgehakten sollen heute nicht nochmal klopfen - auch nicht
    // ueber einen offenen Aufschub dieser Runde.
    remind_snooze_clear(r->minute);
  }
  remind_schedule();
  if (heute) phone_send_today();     // Pins als erledigt markieren
  s_geht = true;

  // Abgeschaltete Animation heisst NICHT, dass das Fenster stehen bleibt:
  // prv_fx_done ist der Weg hinaus, also hier direkt dorthin.
  if (!prefs_fx()) { prv_close_nach_telefon(); return; }

  s_playing = true;
  layer_mark_dirty(s_canvas);
  action_bar_layer_remove_from_window(s_bar);
  const GRect b = layer_get_bounds(s_canvas);
  if (!pill_fx_play(GPoint(b.size.w / 2, b.size.h / 2),
                    (int16_t)(b.size.w * 34 / 100), prv_fx_done)) {
    // Kam nicht zustande: dann eben direkt hinaus, statt auf ein Ende zu
    // warten, das nicht kommt.
    prv_close_nach_telefon();
  }
}

// Wegdruecken: die Runde verfaellt. Kein Aufschub mehr, keine weitere
// Erinnerung dazu - und die App geht ZU, nicht auf den Heute-Schirm. Wer
// nicht nehmen will, will auch nicht die Liste sehen; die kommt, wenn man
// sie selbst oeffnet, und dort laesst sich nachholen, was liegen blieb.
static void prv_dismiss(ClickRecognizerRef recognizer, void *context) {
  if (s_playing || s_geht) return;
  prv_stop_vibes();
  for (int k = 0; k < s_runden_zahl; k++) remind_snooze_clear(s_runden[k].minute);
  remind_schedule();
  prv_close();
}

// ES GIBT EINEN AUFSCHUB, NICHT MEHRERE (remind.h): er gilt der neuesten
// Runde. Aeltere im selben Fenster hatten ihre Erinnerung schon; sie bleiben
// auf dem Heute-Schirm offen, wie nach dem Wegdruecken.
static void prv_later(ClickRecognizerRef recognizer, void *context) {
  if (s_playing || s_geht) return;
  const Runde neueste = *prv_neueste();
  if (!remind_snooze_left(neueste.minute)) {
    // Dreimal "spaeter" heisst "heute nicht": die Runde verfaellt.
    prv_dismiss(recognizer, context);
    return;
  }
  prv_stop_vibes();
  remind_snooze(neueste.minute, neueste.tag);
  prv_close();
}

static void prv_click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_take);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_later);
  window_single_click_subscribe(BUTTON_ID_BACK, prv_dismiss);
}

static void prv_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, prv_canvas_update);
  layer_add_child(root, s_canvas);

  s_bar = action_bar_layer_create();
  action_bar_layer_set_background_color(s_bar, SC_COLOR_SIDEBAR);
  action_bar_layer_set_click_config_provider(s_bar, prv_click_config);
  // Ohne Symbole ist die Leiste ein farbiger Balken - man sieht nicht, was
  // die Tasten tun.
  s_icon_take = gbitmap_create_with_resource(RESOURCE_ID_ICON_CHECK);
  s_icon_later = gbitmap_create_with_resource(RESOURCE_ID_ICON_SNOOZE);
  if (s_icon_take) action_bar_layer_set_icon(s_bar, BUTTON_ID_SELECT, s_icon_take);
  action_bar_layer_add_to_window(s_bar, window);
  prv_klopfen();
}

// Wie im Hauptfenster: der Overlay entsteht beim ERSCHEINEN. Beim Laden
// angelegt, nähme er dem darunterliegenden Hauptschirm seinen weg, noch
// bevor dieses Fenster überhaupt sichtbar ist.
static void prv_appear(Window *window) {
  pill_fx_init(s_canvas);
}

static void prv_unload(Window *window) {
  prv_stop_vibes();
  pill_fx_deinit(s_canvas);
  action_bar_layer_destroy(s_bar);
  if (s_icon_take) { gbitmap_destroy(s_icon_take); s_icon_take = NULL; }
  if (s_icon_later) { gbitmap_destroy(s_icon_later); s_icon_later = NULL; }
  layer_destroy(s_canvas);
  window_destroy(s_window);
  s_window = NULL;
  s_canvas = NULL;
  s_bar = NULL;
  s_geht = false;
  s_danach_zahl = 0;
}

bool reminder_window_push(int minute) {
  const int32_t tag = remind_runden_tag(minute);
  if (s_window) {
    // Das Fenster steht noch. Die neue Runde kommt dazu, wenn es zu ihr
    // etwas zu nehmen gibt.
    const Runde neu = { .minute = minute, .tag = tag };
    if (prv_zahl_in(&neu) == 0) return true;
    if (s_geht) {
      // Schon abgehakt, der Schirm gehoert der Animation und dem Warten auf
      // das Telefon - danach ist sie dran (prv_nach_telefon).
      prv_merken(s_danach, &s_danach_zahl, minute, tag);
      APP_LOG(APP_LOG_LEVEL_INFO, "Runde %d kommt nach dem Warten", minute);
      return true;
    }
    prv_merken(s_runden, &s_runden_zahl, minute, tag);
    APP_LOG(APP_LOG_LEVEL_INFO, "Runde %d kommt zum offenen Fenster dazu", minute);
    prv_klopfen();
    layer_mark_dirty(s_canvas);
    return true;
  }
  s_runden_zahl = 0;
  s_danach_zahl = 0;
  prv_merken(s_runden, &s_runden_zahl, minute, tag);
  if (prv_batch_count() == 0) return false;  // nichts offen: gar nicht erst zeigen

  s_playing = false;
  s_geht = false;
  s_window = window_create();
  window_set_background_color(s_window, SC_COLOR_BG);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_load, .appear = prv_appear, .unload = prv_unload,
  });
  window_stack_push(s_window, true);
  return true;
}
