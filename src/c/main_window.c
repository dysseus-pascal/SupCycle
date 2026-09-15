#include "main_window.h"
#include "theme.h"
#include "phone.h"
#include "pill_fx.h"
#include "plan.h"
#include "prefs.h"
#include "strings.h"

// Ein Fenster, zwei Ansichten — mehr braucht es nicht:
//
//   HEUTE    was ansteht, mit Haken bei dem, was schon genommen ist.
//            Untere Taste wählt, Mitteltaste hakt das Gewählte ab.
//   ZYKLUS   für jedes Präparat die laufende Phase. Obere Taste wechselt hin
//            und zurück.
//
// HEUTE ist der Liste der Systemtimeline nachgebaut - nicht aus dem
// Gedächtnis, sondern nach einem Emulator-Screenshot von ihr: LECO-Zeit,
// darunter der Name fett, darunter ein gedämpfter Untertitel. Drei grosse
// Zeilen statt einer gedrängten, und entsprechend wenige auf einmal.
//
// Die Auswahl ist ein Dreieck in der FARBE DER LEISTE, das nach LINKS auf den
// weissen Grund hinausragt - genau wie dort. Eine weisse Kerbe IN der Leiste
// wäre das Gegenteil: sie nähme der Leiste Platz, statt auf den Eintrag zu
// zeigen, und stritte mit dem Hinweis auf gleicher Höhe.
//
// Zwei Fenster wären zwei Dateien und zwei Lebenszyklen für einen Unterschied,
// den eine Zeile Zustand abbildet.

// Ein Eintrag: Zeit, Name, Untertitel. Die Höhen stehen hier zusammen, damit
// sich Zeilenhöhe und Fensterhöhe nicht getrennt auseinanderentwickeln.
#define WIDE       (PBL_DISPLAY_WIDTH >= 180)
#define LINE_TIME  (WIDE ? 22 : 20)
#define LINE_NAME  (WIDE ? 28 : 22)
#define LINE_SUB   (WIDE ? 20 : 16)
#define ROW_H      (LINE_TIME + LINE_NAME + LINE_SUB + (WIDE ? 4 : 0))

// Die Pfeilspitze der Auswahl. Sitzt mit der Grundlinie auf der linken Kante
// der Leiste und zeigt nach links auf den Eintrag.
//
// Die Masse sind an der Systemtimeline abgemessen, nicht geschätzt: auf einem
// 200 Pixel breiten Schirm ragt ihr Pfeil 13 Pixel heraus und ist 25 hoch,
// bei einer ebenfalls 34 Pixel breiten Leiste. Schmaler wirkte er wie ein
// Versehen statt wie ein Zeiger.
#define ARROW_W    (WIDE ? 14 : 10)
#define ARROW_H    (WIDE ? 14 : 11)

static Window *s_window;
static Layer *s_canvas;
static Layer *s_sidebar;
static bool s_cycle_view;

static int s_sel;            // gewählter Eintrag, Index in den Plan
static int s_first;          // erster sichtbarer Eintrag, Position in der Fälligenliste
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

// Die Pfeilspitze der Auswahl. Sie wird vom Leinwand-Layer gezeichnet, nicht
// von der Leiste: sie ragt nach links über deren Kante hinaus und würde dort
// abgeschnitten. Die Leinwand geht über die ganze Breite.
static void prv_draw_arrow(GContext *ctx, GRect b, int16_t cy) {
  const int16_t x = b.size.w - SC_SIDEBAR_W;
  GPoint pts[3] = { GPoint(x, cy - ARROW_H), GPoint(x - ARROW_W, cy),
                    GPoint(x, cy + ARROW_H) };
  const GPathInfo info = { .num_points = 3, .points = pts };
  GPath *arrow = gpath_create(&info);
  if (!arrow) return;
  graphics_context_set_fill_color(ctx, SC_COLOR_SIDEBAR);
  gpath_draw_filled(ctx, arrow);
  gpath_destroy(arrow);
}

// Einen Strich durch den Text ziehen - so lang wie der Text wirklich ist,
// nicht so lang wie sein Kasten. Die gemessene Breite kommt aus derselben
// Schrift und demselben Kasten wie die Ausgabe, sonst stimmte der Strich bei
// einem abgeschnittenen Namen nicht.
static void prv_strike(GContext *ctx, const char *text, GFont font, GRect box) {
  const GSize sz = graphics_text_layout_get_content_size(
      text, font, box, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  int16_t w = sz.w;
  if (w > box.size.w) w = box.size.w;
  if (w < 2) return;
  // Sechs Zehntel, nicht die Haelfte: die Schrift sitzt im Kasten mit Vorlauf
  // oben, die Mitte des Kastens liegt also ueber der Mitte der Buchstaben.
  // Nachgemessen stehen die Zeichen auf 62..75, der Strich gehoert auf 68.
  const int16_t cy = box.origin.y + LINE_NAME * 6 / 10;
  graphics_context_set_stroke_color(ctx, SC_COLOR_TEXT);
  graphics_context_set_stroke_width(ctx, WIDE ? 2 : 1);
  graphics_draw_line(ctx, GPoint(box.origin.x, cy), GPoint(box.origin.x + w, cy));
  graphics_context_set_stroke_width(ctx, 1);
}

// "täglich" oder "alle X Tage".
static void prv_every_text(uint8_t every, char *out, size_t n) {
  if (every <= 1) snprintf(out, n, "%s", S(STR_DAILY));
  else snprintf(out, n, S(STR_EVERY_FMT), (int)every);
}

// Die dritte Zeile eines Eintrags: was über ihn zu sagen ist. Genommenes sagt
// es selbst, Zyklisches nennt die Phase, Dauerhaftes bleibt bei "täglich".
static void prv_sub_text(int i, char *out, size_t n) {
  // Kein "genommen" mehr: der Strich durch den Namen sagt es bereits, und
  // zweimal dasselbe zu sagen macht es nicht richtiger. Die Zeile bleibt
  // stattdessen beim Rhythmus - der gilt weiter, auch wenn heute erledigt
  // ist, und eine leere Zeile waere nur verschenkter Platz.
  const PlanItem *it = plan_item(i);
  if (!it) { snprintf(out, n, "%s", S(STR_DAILY)); return; }
  const CycleState c = plan_cycle(i);
  // of_weeks == 0: kein Zyklus. Dann sagt die Zeile das Raster, denn sonst
  // hätte sie nichts zu sagen.
  if (c.of_weeks == 0) { prv_every_text(it->every, out, n); return; }
  if (c.phase == CyclePhaseOn) snprintf(out, n, S(STR_ON_FMT), c.week, c.of_weeks);
  else snprintf(out, n, "%s", S(STR_PAUSE));
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
  // Auf der Heute-Liste ein knapperer Rand als sonst: der Name steht hier in
  // fetter 24er Schrift, und "Multivitamin" braucht jeden Pixel. Die
  // Systemtimeline hält sich links ebenso knapp.
  const int16_t margin = PBL_IF_ROUND_ELSE(SC_MARGIN, 6);
  // Der Text hört vor der Pfeilspitze auf. Sonst liefe ein langer Name unter
  // sie - und zwar nur beim gewählten Eintrag, also sprunghaft.
  const int16_t col_w = b.size.w - SC_SIDEBAR_W - margin - ARROW_W;
  int16_t y = PBL_IF_ROUND_ELSE(30, 6);

  if (plan_count() == 0) {
    graphics_context_set_text_color(ctx, SC_COLOR_TEXT);
    graphics_draw_text(ctx, S(STR_NO_PLAN),
                       fonts_get_system_font(WIDE ? FONT_KEY_GOTHIC_24_BOLD
                                                  : FONT_KEY_GOTHIC_18_BOLD),
                       GRect(margin, y, col_w, WIDE ? 30 : 24),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    y += WIDE ? 32 : 26;
    graphics_context_set_text_color(ctx, SC_COLOR_SUB);
    graphics_draw_text(ctx, S(STR_NO_PLAN_SUB),
                       fonts_get_system_font(WIDE ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_14),
                       GRect(margin, y, col_w, b.size.h - y - 4),
                       GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
    return;
  }

  // Kopfzeile: wie viele noch offen sind. Bewusst leise - sie zählt nur, was
  // die Liste darunter ohnehin zeigt, und die Liste ist das Eigentliche.
  const int open = plan_open_today();
  int due_idx[SC_MAX_ITEMS], n = 0;
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    if (plan_due_today(i)) due_idx[n++] = i;
  }
  char head[40];
  if (n == 0) snprintf(head, sizeof(head), "%s", S(STR_NOTHING_DUE));
  else if (open == 0) snprintf(head, sizeof(head), "%s", S(STR_ALL_DONE));
  else snprintf(head, sizeof(head), S(STR_OPEN_FMT), open, n);

  graphics_context_set_text_color(ctx, SC_COLOR_SUB);
  graphics_draw_text(ctx, head,
                     fonts_get_system_font(WIDE ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_14),
                     GRect(margin, y, col_w, WIDE ? 22 : 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  y += WIDE ? 24 : 19;

  if (n == 0) return;

  // Das Fenster der Liste der Auswahl nachziehen. Grosse Einträge heissen
  // wenige sichtbare - ohne Nachziehen liefe die Auswahl unten hinaus und man
  // wählte blind weiter.
  prv_fix_selection();
  int sel_pos = -1;
  for (int k = 0; k < n; k++) {
    if (due_idx[k] == s_sel) sel_pos = k;
  }
  const int vis = (b.size.h - y - 2) / ROW_H;
  if (vis >= 1) {
    if (sel_pos >= 0 && sel_pos < s_first) s_first = sel_pos;
    if (sel_pos >= 0 && sel_pos > s_first + vis - 1) s_first = sel_pos - vis + 1;
    if (s_first > n - vis) s_first = n - vis;
  }
  if (s_first < 0) s_first = 0;

  for (int k = s_first; k < n && y + ROW_H <= b.size.h; k++) {
    const int i = due_idx[k];
    const PlanItem *it = plan_item(i);
    const bool taken = plan_taken(i);
    // Erledigtes wird GESTRICHEN, nicht ausgegraut. Grau auf einem Schirm,
    // der spiegelt statt zu leuchten, liest sich als ausgewaschen und nicht
    // als erledigt. Ein Strich ist dieselbe Geste wie auf einer Liste aus
    // Papier und bleibt bei jedem Licht deutlich.
    const GColor fg = SC_COLOR_TEXT;

    // Zeit in LECO, wie im Kopf eines Timeline-Eintrags
    char when[8];
    snprintf(when, sizeof(when), "%02d:%02d", it->hour, it->minute);
    graphics_context_set_text_color(ctx, fg);
    graphics_draw_text(ctx, when, fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS),
                       GRect(margin, y, col_w, LINE_TIME + 4),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    // Der Haken steht neben der Zeit, nicht vor dem Namen: dort bliebe für
    // "Multivitamin" in fetter 24er Schrift nichts mehr übrig.
    if (taken) prv_draw_check(ctx, GPoint(margin + (WIDE ? 62 : 56), y + 4), 12);

    GFont name_font = fonts_get_system_font(WIDE ? FONT_KEY_GOTHIC_24_BOLD
                                                 : FONT_KEY_GOTHIC_18_BOLD);
    const GRect name_box = GRect(margin, y + LINE_TIME, col_w, LINE_NAME + 2);
    graphics_draw_text(ctx, it->name, name_font, name_box,
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    if (taken) prv_strike(ctx, it->name, name_font, name_box);

    char sub[40];
    prv_sub_text(i, sub, sizeof(sub));
    graphics_context_set_text_color(ctx, SC_COLOR_SUB);
    graphics_draw_text(ctx, sub,
                       fonts_get_system_font(WIDE ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_14),
                       GRect(margin, y + LINE_TIME + LINE_NAME, col_w, LINE_SUB + 2),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    if (i == s_sel) prv_draw_arrow(ctx, b, y + ROW_H / 2);
    y += ROW_H;
  }
}

// Höhe einer Zeile in der Zyklusansicht, gross und klein. Die grosse ist so
// knapp geschnitten, wie die 24er Schrift und ihr Untertitel es zulassen -
// vier Präparate sind ein gewöhnlicher Stack, und die sollen gross dastehen.
#define CYC_BIG    (WIDE ? 48 : 40)
#define CYC_SMALL  (WIDE ? 40 : 34)

static void prv_draw_cycle(GContext *ctx, GRect b) {
  const int16_t margin = SC_MARGIN;
  const int16_t col_w = b.size.w - SC_SIDEBAR_W - margin - 4;
  int16_t y = PBL_IF_ROUND_ELSE(30, 6);

  graphics_context_set_text_color(ctx, SC_COLOR_SUB);
  graphics_draw_text(ctx, S(STR_CYCLE),
                     fonts_get_system_font(WIDE ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_14),
                     GRect(margin, y, col_w, WIDE ? 22 : 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  y += WIDE ? 24 : 19;

  // Gross schreiben, solange ALLE hineinpassen - nicht bis zu einer geratenen
  // Anzahl. Sonst steht das letzte Präparat gross da und sein Untertitel
  // unter dem Bildrand, was schlimmer ist als eine Nummer kleiner.
  const int rows = plan_count();
  const int16_t avail = b.size.h - y - 2;
  const bool big = rows > 0 && (avail / rows) >= CYC_BIG;

  for (int i = 0; i < SC_MAX_ITEMS && y < b.size.h - 8; i++) {
    const PlanItem *it = plan_item(i);
    if (!it || !it->used) continue;

    graphics_context_set_text_color(ctx, SC_COLOR_TEXT);
    graphics_draw_text(ctx, it->name,
                       fonts_get_system_font(big ? FONT_KEY_GOTHIC_24_BOLD
                                                 : FONT_KEY_GOTHIC_18_BOLD),
                       GRect(margin, y, col_w, big ? 28 : 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    y += big ? CYC_BIG / 2 : CYC_SMALL / 2;

    char sub[64];
    const CycleState probe = plan_cycle(i);
    if (probe.of_weeks == 0) {
      // Unbegrenzt: Raster nennen und dazusagen, dass es keine Pause gibt.
      char ev[32];
      prv_every_text(it->every, ev, sizeof(ev));
      snprintf(sub, sizeof(sub), "%s, %s", ev, S(STR_UNLIMITED));
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
    graphics_context_set_text_color(ctx, SC_COLOR_SUB);
    graphics_draw_text(ctx, sub,
                       fonts_get_system_font(big ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_14),
                       GRect(margin, y, col_w, big ? 22 : 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    y += big ? CYC_BIG - CYC_BIG / 2 : CYC_SMALL - CYC_SMALL / 2;
  }
}

static void prv_canvas_update(Layer *layer, GContext *ctx) {
  // Während der Animation gehört der Schirm ihr. Sonst liefe Pilly über der
  // Liste, und der Strahlenkranz verschwände zwischen den Zeilen.
  if (s_playing) return;
  const GRect b = layer_get_bounds(layer);

  // Keine kleine Uhrzeit mehr: die LISTE der Systemtimeline hat keine, nur
  // das Pin-Detail. Die sechzehn Pixel gehören dem ersten Eintrag.
  if (s_cycle_view) prv_draw_cycle(ctx, b);
  else prv_draw_today(ctx, b);
}

// Ein Hinweis in der Seitenleiste, mittig um cy. Über die volle Breite: der
// Auswahlpfeil steht jetzt ausserhalb der Leiste und nimmt ihr nichts mehr weg.
static void prv_hint(GContext *ctx, GRect b, const char *text, int16_t cy) {
  graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(0, cy - 9, b.size.w, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void prv_sidebar_update(Layer *layer, GContext *ctx) {
  if (s_playing) return;    // auch die Leiste weicht der Animation
  const GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, SC_COLOR_SIDEBAR);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, SC_COLOR_ON_SIDEBAR);

  // Auf der runden Uhr rücken der obere und der untere Hinweis zur Mitte: dort
  // ist die Leiste breit. Auf Vierteln schneidet der Kreis sie zu "Cycl" und
  // "Nex" ab - ein abgeschnittenes Wort ist schlimmer als ein enger Abstand.
  const int16_t hy1 = PBL_IF_ROUND_ELSE(b.size.h * 34 / 100, b.size.h / 4);
  const int16_t hy3 = PBL_IF_ROUND_ELSE(b.size.h * 66 / 100, b.size.h * 3 / 4);

  // Obere Taste: zwischen den Ansichten wechseln
  prv_hint(ctx, b, s_cycle_view ? S(STR_HINT_BACK) : S(STR_HINT_CYCLE), hy1);

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
      prv_hint(ctx, b, S(STR_HINT_NEXT), hy3);
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
  }
  // Gefeiert wird die EINNAHME, nicht die einzelne Tablette: stehen Kreatin
  // und Vitamine beide auf acht Uhr, spielt Pilly erst, wenn beide abgehakt
  // sind. Und nur, wenn die Animation eingeschaltet ist.
  if (!taken && prefs_fx() && plan_slot_complete(s_sel)) {
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
