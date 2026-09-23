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

#define VIBE_PULSES 3
#define VIBE_GAP_MS 20000

static Window *s_window;
static Layer *s_canvas;
static ActionBarLayer *s_bar;
static GBitmap *s_icon_take;
static GBitmap *s_icon_later;
static AppTimer *s_vibe;
static int s_vibes_left;
static int s_minute;          // Uhrzeit der Erinnerung, -1 = "was offen ist"
static bool s_playing;

// Gehört dieses Präparat zu dieser Erinnerung? Bei -1 (nach einem Aufschub
// oder ohne Uhrzeit) zählt alles, was heute noch offen ist.
static bool prv_in_batch(int i) {
  if (!plan_due_today(i) || plan_taken(i)) return false;
  if (s_minute < 0) return true;
  const PlanItem *it = plan_item(i);
  return it && (it->hour * 60 + it->minute) == s_minute;
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

  char hhmm[8];
  if (s_minute >= 0) {
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", s_minute / 60, s_minute % 60);
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
  const int count = remind_snooze_count(s_minute);
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

static void prv_fx_done(void) {
  s_playing = false;
  prv_close();
}

static void prv_take(ClickRecognizerRef recognizer, void *context) {
  if (s_playing) return;
  prv_stop_vibes();
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    if (prv_in_batch(i)) plan_set_taken(i, true);
  }
  // Wecker neu stellen: die eben abgehakten sollen heute nicht nochmal
  // klopfen - auch nicht ueber einen offenen Aufschub.
  remind_snooze_clear();
  remind_schedule();
  phone_send_today();     // Pins als erledigt markieren

  // Abgeschaltete Animation heisst NICHT, dass das Fenster stehen bleibt:
  // prv_fx_done ist der Weg hinaus, also hier direkt gehen.
  if (!prefs_fx()) { prv_close(); return; }

  s_playing = true;
  layer_mark_dirty(s_canvas);
  action_bar_layer_remove_from_window(s_bar);
  const GRect b = layer_get_bounds(s_canvas);
  if (!pill_fx_play(GPoint(b.size.w / 2, b.size.h / 2),
                    (int16_t)(b.size.w * 34 / 100), prv_fx_done)) {
    // Kam nicht zustande: dann eben direkt hinaus, statt auf ein Ende zu
    // warten, das nicht kommt.
    prv_close();
  }
}

// Wegdruecken: die Runde verfaellt. Kein Aufschub mehr, keine weitere
// Erinnerung dazu - und die App geht ZU, nicht auf den Heute-Schirm. Wer
// nicht nehmen will, will auch nicht die Liste sehen; die kommt, wenn man
// sie selbst oeffnet, und dort laesst sich nachholen, was liegen blieb.
static void prv_dismiss(ClickRecognizerRef recognizer, void *context) {
  if (s_playing) return;
  prv_stop_vibes();
  remind_snooze_clear();
  remind_schedule();
  prv_close();
}

static void prv_later(ClickRecognizerRef recognizer, void *context) {
  if (s_playing) return;
  if (!remind_snooze_left(s_minute)) {
    // Dreimal "spaeter" heisst "heute nicht": die Runde verfaellt.
    prv_dismiss(recognizer, context);
    return;
  }
  prv_stop_vibes();
  remind_snooze(s_minute);
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
  // Kein Aufschub mehr uebrig: dann steht dort auch kein Zeichen dafuer. Ein
  // Zeichen fuer eine Taste, die etwas anderes tut, waere eine Luege.
  if (s_icon_later && remind_snooze_left(s_minute)) {
    action_bar_layer_set_icon(s_bar, BUTTON_ID_DOWN, s_icon_later);
  }
  action_bar_layer_add_to_window(s_bar, window);

  s_vibes_left = VIBE_PULSES;
  prv_vibe_cb(NULL);
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
}

bool reminder_window_push(int minute) {
  if (s_window) return true;
  s_minute = minute;
  if (prv_batch_count() == 0) return false;  // nichts offen: gar nicht erst zeigen

  s_playing = false;
  s_window = window_create();
  window_set_background_color(s_window, SC_COLOR_BG);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_load, .appear = prv_appear, .unload = prv_unload,
  });
  window_stack_push(s_window, true);
  return true;
}
