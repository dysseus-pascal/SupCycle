#include "main_window.h"
#include "theme.h"
#include "plan.h"
#include "strings.h"

// Ein Fenster, zwei Ansichten — mehr braucht es nicht:
//
//   HEUTE    was ansteht, mit Haken bei dem, was schon genommen ist.
//            Mitteltaste hakt den nächsten offenen ab.
//   ZYKLUS   für jedes Präparat die laufende Phase. Obere Taste wechselt hin
//            und zurück.
//
// Zwei Fenster wären zwei Dateien und zwei Lebenszyklen für einen Unterschied,
// den eine Zeile Zustand abbildet.

#define ROW_H  (PBL_DISPLAY_HEIGHT >= 200 ? 30 : 26)

static Window *s_window;
static Layer *s_canvas;
static Layer *s_sidebar;
static bool s_cycle_view;

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
  for (int i = 0; i < SC_MAX_ITEMS && y < b.size.h - 8; i++) {
    if (!plan_due_today(i)) continue;
    const PlanItem *it = plan_item(i);
    const bool taken = plan_taken(i);

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

static void prv_sidebar_update(Layer *layer, GContext *ctx) {
  const GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, SC_COLOR_SIDEBAR);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, SC_COLOR_ON_SIDEBAR);

  // Obere Taste: zwischen den Ansichten wechseln
  graphics_draw_text(ctx, s_cycle_view ? S(STR_HINT_BACK) : S(STR_HINT_CYCLE),
                     fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(0, b.size.h / 4 - 9, b.size.w, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // Mitteltaste: abhaken, nur in der Heute-Ansicht und nur wenn offen
  if (!s_cycle_view && plan_next_open() >= 0) {
    graphics_draw_text(ctx, S(STR_HINT_TAKE),
                       fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(0, b.size.h / 2 - 9, b.size.w, 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

static void prv_select(ClickRecognizerRef recognizer, void *context) {
  if (s_cycle_view) return;
  const int next = plan_next_open();
  if (next < 0) return;
  plan_set_taken(next, true);
  vibes_short_pulse();
  main_window_refresh();
}

static void prv_up(ClickRecognizerRef recognizer, void *context) {
  s_cycle_view = !s_cycle_view;
  main_window_refresh();
}

static void prv_click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select);
  window_single_click_subscribe(BUTTON_ID_UP, prv_up);
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
  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick);
}

static void prv_unload(Window *window) {
  tick_timer_service_unsubscribe();
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
