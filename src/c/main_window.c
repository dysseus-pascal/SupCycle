#include "main_window.h"
#include "theme.h"
#include "phone.h"
#include "pill_fx.h"
#include "plan.h"
#include "strings.h"

// Ein Fenster, zwei Ansichten — mehr braucht es nicht:
//
//   HEUTE    was ansteht, mit Haken bei dem, was schon genommen ist.
//            Untere Taste wählt, Mitteltaste hakt das Gewählte ab.
//   ZYKLUS   für jedes Präparat die laufende Phase. Obere Taste wechselt hin
//            und zurück.
//
// Die Auswahl zeigt eine weisse Pfeilkerbe in der Seitenleiste, wie die
// Timeline sie am gewählten Eintrag hat. Ihre Höhe wird beim Zeichnen der
// Liste festgehalten (s_sel_y) und von der Leiste übernommen - beide Schichten
// rechnen so nicht getrennt an derselben Zeile herum.
//
// Zwei Fenster wären zwei Dateien und zwei Lebenszyklen für einen Unterschied,
// den eine Zeile Zustand abbildet.

#define ROW_H  (PBL_DISPLAY_HEIGHT >= 200 ? 30 : 26)

static Window *s_window;
static Layer *s_canvas;
static Layer *s_sidebar;
static bool s_cycle_view;

// Die Pfeilkerbe in der Seitenleiste. Schmal gehalten, weil die Leiste selbst
// nur 30 bis 34 Pixel misst und ein Hinweis auf gleicher Höhe um genau diese
// Breite ausweichen muss.
#define NOTCH_W 6
#define NOTCH_H 8

static int s_sel;            // gewählter Eintrag, Index in den Plan
static int16_t s_sel_y = -1; // Bildmitte der gewählten Zeile, -1 = nicht sichtbar
static bool s_playing;       // läuft gerade die Genommen-Animation?

// Haken, von Hand gezeichnet: zwei Striche. Ein Bildsymbol dafür wäre eine
// Ressourcendatei für achtzehn Pixel.
static void prv_draw_check(GContext *ctx, GPoint at, int16_t size) {
  graphics_context_set_stroke_color(ctx, SC_COLOR_BAR);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, GPoint(at.x, at.y + size / 2),
                          GPoint(at.x + size / 3, at.y + size));
  graphics_draw_line(ctx, GPoint(at.x + size / 3, at.y + size),
                          GPoint(at.x + size, at.y));
  graphics_context_set_stroke_width(ctx, 1);
}

// Auf einen heute fälligen Eintrag zeigen. Ist der gemerkte keiner mehr -
// etwa weil ein Zyklus über Nacht in die Pause ging -, auf den ersten
// fälligen zurückfallen.
static void prv_fix_selection(void) {
  if (s_sel >= 0 && s_sel < SC_MAX_ITEMS && plan_due_today(s_sel)) return;
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    if (plan_due_today(i)) { s_sel = i; return; }
  }
  s_sel = -1;
}

// Nächster heute fälliger Eintrag nach dem gewählten, rundum.
static void prv_select_next(void) {
  for (int step = 1; step <= SC_MAX_ITEMS; step++) {
    const int i = (s_sel + step + SC_MAX_ITEMS) % SC_MAX_ITEMS;
    if (plan_due_today(i)) { s_sel = i; return; }
  }
}

static void prv_draw_today(GContext *ctx, GRect b) {
  const bool wide = PBL_DISPLAY_WIDTH >= 180;
  const int16_t margin = SC_MARGIN;
  const int16_t col_w = b.size.w - SC_SIDEBAR_W - margin - 4;
  int16_t y = PBL_IF_ROUND_ELSE(40, 16);

  if (plan_count() == 0) {
    graphics_context_set_text_color(ctx, SC_COLOR_TEXT);
    graphics_draw_text(ctx, S(STR_NO_PLAN),
                       fonts_get_system_font(wide ? FONT_KEY_GOTHIC_24_BOLD
                                                  : FONT_KEY_GOTHIC_18_BOLD),
                       GRect(margin, y, col_w, wide ? 30 : 24),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    y += wide ? 32 : 26;
    graphics_context_set_text_color(ctx, SC_COLOR_DIM);
    graphics_draw_text(ctx, S(STR_NO_PLAN_SUB),
                       fonts_get_system_font(wide ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_14),
                       GRect(margin, y, col_w, b.size.h - y - 4),
                       GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
    return;
  }

  // Kopfzeile: wie viele noch offen sind
  const int open = plan_open_today();
  int due = 0;
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    if (plan_due_today(i)) due++;
  }
  char head[40];
  if (due == 0) snprintf(head, sizeof(head), "%s", S(STR_NOTHING_DUE));
  else if (open == 0) snprintf(head, sizeof(head), "%s", S(STR_ALL_DONE));
  else snprintf(head, sizeof(head), S(STR_OPEN_FMT), open, due);

  graphics_context_set_text_color(ctx, open == 0 ? SC_COLOR_DIM : SC_COLOR_TEXT);
  graphics_draw_text(ctx, head,
                     fonts_get_system_font(wide ? FONT_KEY_GOTHIC_18_BOLD
                                                : FONT_KEY_GOTHIC_14_BOLD),
                     GRect(margin, y, col_w, 24),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  y += wide ? 26 : 22;

  // Ein Eintrag je Zeile. Was heute nicht ansteht, steht gar nicht da - in der
  // Pause will man nicht daran erinnert werden, dass man pausiert.
  prv_fix_selection();
  s_sel_y = -1;
  for (int i = 0; i < SC_MAX_ITEMS && y < b.size.h - 8; i++) {
    if (!plan_due_today(i)) continue;
    const PlanItem *it = plan_item(i);
    const bool taken = plan_taken(i);
    if (i == s_sel) s_sel_y = y + ROW_H / 2;

    if (taken) prv_draw_check(ctx, GPoint(margin, y + 4), 12);

    char line[40];
    snprintf(line, sizeof(line), "%02d:%02d  %s", it->hour, it->minute, it->name);
    graphics_context_set_text_color(ctx, taken ? SC_COLOR_DIM : SC_COLOR_TEXT);
    graphics_draw_text(ctx, line,
                       fonts_get_system_font(taken ? FONT_KEY_GOTHIC_18
                                                   : FONT_KEY_GOTHIC_18_BOLD),
                       GRect(margin + (taken ? 20 : 0), y, col_w - (taken ? 20 : 0), ROW_H),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    y += ROW_H;
  }
}

static void prv_draw_cycle(GContext *ctx, GRect b) {
  const bool wide = PBL_DISPLAY_WIDTH >= 180;
  const int16_t margin = SC_MARGIN;
  const int16_t col_w = b.size.w - SC_SIDEBAR_W - margin - 4;
  int16_t y = PBL_IF_ROUND_ELSE(40, 16);

  graphics_context_set_text_color(ctx, SC_COLOR_TEXT);
  graphics_draw_text(ctx, S(STR_CYCLE), fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(margin, y, col_w, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  y += 18;

  for (int i = 0; i < SC_MAX_ITEMS && y < b.size.h - 8; i++) {
    const PlanItem *it = plan_item(i);
    if (!it || it->mode == PlanUnused) continue;

    graphics_context_set_text_color(ctx, SC_COLOR_TEXT);
    graphics_draw_text(ctx, it->name, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(margin, y, col_w, 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    y += 20;

    char sub[48];
    if (it->mode == PlanDaily) {
      snprintf(sub, sizeof(sub), "%s", S(STR_DAILY));
    } else {
      const CycleState c = plan_cycle(i);
      char phase[32];
      if (c.phase == CyclePhaseOn) {
        snprintf(phase, sizeof(phase), S(STR_ON_FMT), c.week, c.of_weeks);
      } else {
        snprintf(phase, sizeof(phase), "%s", S(STR_PAUSE));
      }
      char rest[24] = "";
      if (c.days_left == 1) snprintf(rest, sizeof(rest), "%s", S(STR_DAY_LEFT));
      else if (c.days_left > 1) snprintf(rest, sizeof(rest), S(STR_DAYS_LEFT), c.days_left);
      if (rest[0]) snprintf(sub, sizeof(sub), "%s, %s", phase, rest);
      else snprintf(sub, sizeof(sub), "%s", phase);
    }
    graphics_context_set_text_color(ctx, SC_COLOR_DIM);
    graphics_draw_text(ctx, sub, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(margin, y, col_w, 20),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    y += wide ? 22 : 20;
  }
}

static void prv_canvas_update(Layer *layer, GContext *ctx) {
  // Während der Animation gehört der Schirm ihr. Sonst liefe Pilly über der
  // Liste, und der Strahlenkranz verschwände zwischen den Zeilen.
  if (s_playing) return;
  const GRect b = layer_get_bounds(layer);

  // Kleine Uhrzeit oben, wie im Kopf eines Timeline-Eintrags
  graphics_context_set_text_color(ctx, SC_COLOR_TEXT);
  char clock[10];
  clock_copy_time_string(clock, sizeof(clock));
  graphics_draw_text(ctx, clock, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(0, PBL_IF_ROUND_ELSE(10, 0), b.size.w - SC_SIDEBAR_W, 16),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  if (s_cycle_view) prv_draw_cycle(ctx, b);
  else prv_draw_today(ctx, b);
}

// Ein Hinweis in der Seitenleiste, mittig um cy.
//
// Eingerückt wird NUR, wenn die Pfeilkerbe auf derselben Höhe liegt. Ein
// pauschaler Einzug wäre ruhiger, kostete aber überall Platz: die Leiste misst
// 30 bis 34 Pixel, und "Cycle" bricht dann zu "Cy...". Lieber weicht das eine
// Wort aus, neben dem der Pfeil tatsächlich steht - das liest sich als Antwort
// auf den Pfeil und nicht als Fehler.
static void prv_hint(GContext *ctx, GRect b, const char *text, int16_t cy) {
  const bool hit = (s_sel_y >= 0) && (s_sel_y - cy < 18) && (cy - s_sel_y < 18);
  const int16_t x = hit ? NOTCH_W : 0;
  graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(x, cy - 9, b.size.w - x, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void prv_sidebar_update(Layer *layer, GContext *ctx) {
  if (s_playing) return;    // auch die Leiste weicht der Animation
  const GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, SC_COLOR_SIDEBAR);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, SC_COLOR_ON_SIDEBAR);

  // Obere Taste: zwischen den Ansichten wechseln
  prv_hint(ctx, b, s_cycle_view ? S(STR_HINT_BACK) : S(STR_HINT_CYCLE), b.size.h / 4);

  if (!s_cycle_view) {
    // Mitteltaste: das Gewählte abhaken - oder den Haken zurücknehmen.
    if (s_sel >= 0) {
      prv_hint(ctx, b, plan_taken(s_sel) ? S(STR_HINT_UNDO) : S(STR_HINT_TAKE), b.size.h / 2);
    }
    // Untere Taste: weiterwählen. Nur wenn es überhaupt etwas zu wählen gibt.
    int due = 0;
    for (int i = 0; i < SC_MAX_ITEMS; i++) {
      if (plan_due_today(i)) due++;
    }
    if (due > 1) {
      prv_hint(ctx, b, S(STR_HINT_NEXT), b.size.h * 3 / 4);
    }

    // Weisse Pfeilkerbe am gewählten Eintrag, wie in der Timeline.
    if (s_sel_y >= 0) {
      GPoint pts[3] = { GPoint(0, s_sel_y - NOTCH_H), GPoint(NOTCH_W, s_sel_y),
                        GPoint(0, s_sel_y + NOTCH_H) };
      const GPathInfo info = { .num_points = 3, .points = pts };
      GPath *notch = gpath_create(&info);
      if (notch) {
        graphics_context_set_fill_color(ctx, GColorWhite);
        gpath_draw_filled(ctx, notch);
        gpath_destroy(notch);
      }
    }
  }
}

// Mitteltaste: das Gewählte abhaken - oder den Haken zurücknehmen.
//
// Zurücknehmen ist hier richtig, anders als beim Glas in Drinktervall: ein
// getrunkenes Glas lässt sich nicht ungetrunken machen, ein Fehlgriff auf der
// Uhr aber sehr wohl. Und ein falscher Haken im Plan ist schlimmer als keiner:
// er sagt, man habe genommen, was man nicht genommen hat.
static void prv_fx_done(void) {
  s_playing = false;
  main_window_refresh();
}

static void prv_select(ClickRecognizerRef recognizer, void *context) {
  if (s_cycle_view || s_playing) return;
  prv_fix_selection();
  if (s_sel < 0) return;
  const bool taken = plan_taken(s_sel);
  plan_set_taken(s_sel, !taken);
  phone_send_today();     // Pin nachziehen

  if (!taken) {
    // Genommen: Pilly spielt. Beim Zurücknehmen nicht - eine Feier für einen
    // Fehlgriff wäre verkehrt herum.
    vibes_short_pulse();
    s_playing = true;
    const GRect b = layer_get_bounds(s_canvas);
    pill_fx_play(GPoint(b.size.w / 2, b.size.h / 2),
                 (int16_t)(b.size.w * 34 / 100), prv_fx_done);
  }
  main_window_refresh();
}

static void prv_up(ClickRecognizerRef recognizer, void *context) {
  s_cycle_view = !s_cycle_view;
  main_window_refresh();
}

static void prv_down(ClickRecognizerRef recognizer, void *context) {
  if (s_cycle_view) return;
  prv_fix_selection();
  prv_select_next();
  main_window_refresh();
}

static void prv_click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select);
  window_single_click_subscribe(BUTTON_ID_UP, prv_up);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_down);
}

static void prv_tick(struct tm *tick_time, TimeUnits units_changed) {
  main_window_refresh();
}

static void prv_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  const GRect bounds = layer_get_bounds(root);
  s_canvas = layer_create(bounds);
  layer_set_update_proc(s_canvas, prv_canvas_update);
  layer_add_child(root, s_canvas);
  s_sidebar = layer_create(GRect(bounds.size.w - SC_SIDEBAR_W, 0,
                                 SC_SIDEBAR_W, bounds.size.h));
  layer_set_update_proc(s_sidebar, prv_sidebar_update);
  layer_add_child(root, s_sidebar);
  // Zuletzt, damit die Animation über allem liegt.
  pill_fx_init(root);
  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick);
}

static void prv_unload(Window *window) {
  tick_timer_service_unsubscribe();
  pill_fx_deinit();
  s_playing = false;
  layer_destroy(s_sidebar);
  layer_destroy(s_canvas);
  window_destroy(s_window);
  s_window = NULL;
  s_canvas = NULL;
  s_sidebar = NULL;
}

void main_window_refresh(void) {
  if (s_canvas) layer_mark_dirty(s_canvas);
  if (s_sidebar) layer_mark_dirty(s_sidebar);
}

void main_window_push(void) {
  if (s_window) return;
  s_window = window_create();
  window_set_background_color(s_window, SC_COLOR_BG);
  window_set_click_config_provider(s_window, prv_click_config);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_load, .unload = prv_unload,
  });
  window_stack_push(s_window, true);
}
