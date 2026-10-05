// Fenster, Ebenen, Tasten, Animation fuer den Test der ganzen App
// (pebble_ui.h). Der Ablauf folgt pebbleos applib/ui/window_stack.c und
// applib/app.c; gezeichnet wird nichts, nur die Texte werden mitgeschrieben.
#include <pebble.h>
#include <stdlib.h>

// --- Zeichnen ---
struct AttrappeSchrift { int dummy; };
static struct AttrappeSchrift s_schrift;
static char s_texte[4096];

GFont fonts_get_system_font(const char *font_key) { (void)font_key; return &s_schrift; }
void graphics_context_set_text_color(GContext *ctx, GColor color) { (void)ctx; (void)color; }
void graphics_context_set_fill_color(GContext *ctx, GColor color) { (void)ctx; (void)color; }
void graphics_context_set_stroke_color(GContext *ctx, GColor color) { (void)ctx; (void)color; }
void graphics_context_set_stroke_width(GContext *ctx, uint8_t width) { (void)ctx; (void)width; }
void graphics_draw_text(GContext *ctx, const char *text, GFont font, GRect box,
                        GTextOverflowMode overflow, GTextAlignment alignment, GTextAttributes *attributes) {
  (void)ctx; (void)font; (void)box; (void)overflow; (void)alignment; (void)attributes;
  const size_t l = strlen(s_texte);
  snprintf(s_texte + l, sizeof(s_texte) - l, "%s|", text ? text : "(null)");
}
GSize graphics_text_layout_get_content_size(const char *text, GFont font, GRect box,
                                            GTextOverflowMode overflow, GTextAlignment alignment) {
  (void)font; (void)overflow; (void)alignment;
  // Grob wie Gothic 24: 11 Pixel je Zeichen, eine Zeile.
  const int w = (int)strlen(text ? text : "") * 11;
  return GSize(w < box.size.w ? w : box.size.w, 24);
}
void graphics_fill_rect(GContext *ctx, GRect rect, uint16_t r, GCornerMask m) { (void)ctx; (void)rect; (void)r; (void)m; }
void graphics_draw_line(GContext *ctx, GPoint p0, GPoint p1) { (void)ctx; (void)p0; (void)p1; }
void graphics_draw_round_rect(GContext *ctx, GRect rect, uint16_t r) { (void)ctx; (void)rect; (void)r; }

struct GPath { GPathInfo info; };
GPath *gpath_create(const GPathInfo *init) {
  GPath *p = malloc(sizeof(GPath));
  if (p) p->info = *init;
  return p;
}
void gpath_destroy(GPath *path) { free(path); }
void gpath_draw_filled(GContext *ctx, GPath *path) { (void)ctx; (void)path; }
void gpath_draw_outline(GContext *ctx, GPath *path) { (void)ctx; (void)path; }
void gpath_draw_outline_open(GContext *ctx, GPath *path) { (void)ctx; (void)path; }
int32_t sin_lookup(int32_t angle) { (void)angle; return 0; }
int32_t cos_lookup(int32_t angle) { (void)angle; return TRIG_MAX_RATIO; }

struct GBitmap { uint32_t id; };
GBitmap *gbitmap_create_with_resource(uint32_t resource_id) {
  GBitmap *b = malloc(sizeof(GBitmap));
  if (b) b->id = resource_id;
  return b;
}
void gbitmap_destroy(GBitmap *bitmap) { free(bitmap); }

// --- Ebenen ---
#define KINDER_MAX 8
struct Layer {
  GRect frame;
  LayerUpdateProc proc;
  Layer *eltern;
  Layer *kinder[KINDER_MAX];
  int kinder_zahl;
  bool versteckt;
};
Layer *layer_create(GRect frame) {
  Layer *l = calloc(1, sizeof(Layer));
  if (l) l->frame = frame;
  return l;
}
static void prv_abhaengen(Layer *l) {
  Layer *e = l->eltern;
  if (!e) return;
  for (int i = 0; i < e->kinder_zahl; i++) {
    if (e->kinder[i] != l) continue;
    memmove(&e->kinder[i], &e->kinder[i + 1], (size_t)(e->kinder_zahl - i - 1) * sizeof(Layer *));
    e->kinder_zahl--;
    break;
  }
  l->eltern = NULL;
}
void layer_destroy(Layer *layer) {
  if (!layer) return;
  prv_abhaengen(layer);
  for (int i = 0; i < layer->kinder_zahl; i++) layer->kinder[i]->eltern = NULL;
  free(layer);
}
GRect layer_get_bounds(const Layer *layer) { return GRect(0, 0, layer->frame.size.w, layer->frame.size.h); }
void layer_set_update_proc(Layer *layer, LayerUpdateProc update_proc) { layer->proc = update_proc; }
void layer_add_child(Layer *parent, Layer *child) {
  prv_abhaengen(child);
  if (parent->kinder_zahl >= KINDER_MAX) return;
  parent->kinder[parent->kinder_zahl++] = child;
  child->eltern = parent;
}
void layer_mark_dirty(Layer *layer) { (void)layer; }
void layer_set_hidden(Layer *layer, bool hidden) { layer->versteckt = hidden; }

// --- Fenster ---
struct Window {
  Layer *wurzel;
  WindowHandlers h;
  ClickConfigProvider tasten;
  ActionBarLayer *leiste;
  bool geladen;
};
struct ActionBarLayer { ClickConfigProvider tasten; Window *fenster; };

#define STAPEL_MAX 8
static Window *s_stapel[STAPEL_MAX];
static int s_stapel_zahl;
// Beim Ende der App nimmt pebbleos neue Fenster nicht mehr an
// (window_stack_lock_push in app.c).
static bool s_stapel_gesperrt;
static int s_fenster_beim_ende = -1;

Window *window_create(void) {
  Window *w = calloc(1, sizeof(Window));
  if (w) w->wurzel = layer_create(GRect(0, 0, PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT));
  return w;
}
void window_destroy(Window *window) {
  if (!window) return;
  layer_destroy(window->wurzel);
  free(window);
}
void window_set_background_color(Window *window, GColor color) { (void)window; (void)color; }
void window_set_window_handlers(Window *window, WindowHandlers handlers) { window->h = handlers; }
void window_set_click_config_provider(Window *window, ClickConfigProvider provider) { window->tasten = provider; }
Layer *window_get_root_layer(const Window *window) { return window->wurzel; }

void window_stack_push(Window *window, bool animated) {
  (void)animated;
  if (s_stapel_gesperrt || s_stapel_zahl >= STAPEL_MAX) return;
  Window *vorher = s_stapel_zahl ? s_stapel[s_stapel_zahl - 1] : NULL;
  s_stapel[s_stapel_zahl++] = window;
  if (!window->geladen) {
    window->geladen = true;
    if (window->h.load) window->h.load(window);
  }
  if (vorher && vorher->h.disappear) vorher->h.disappear(vorher);
  if (window->h.appear) window->h.appear(window);
}

// Das oberste Fenster weg: disappear, unload, und das darunter erscheint.
static void prv_pop(bool darunter_erscheint) {
  if (s_stapel_zahl == 0) return;
  Window *w = s_stapel[--s_stapel_zahl];
  if (w->h.disappear) w->h.disappear(w);
  w->geladen = false;
  if (w->h.unload) w->h.unload(w);
  if (darunter_erscheint && s_stapel_zahl) {
    Window *u = s_stapel[s_stapel_zahl - 1];
    if (u->h.appear) u->h.appear(u);
  }
}
void window_stack_pop_all(bool animated) {
  (void)animated;
  while (s_stapel_zahl) prv_pop(false);
}

static ClickHandler s_kurz[NUM_BUTTONS], s_lang[NUM_BUTTONS];
void window_single_click_subscribe(ButtonId button_id, ClickHandler handler) { s_kurz[button_id] = handler; }
void window_long_click_subscribe(ButtonId button_id, uint16_t delay_ms, ClickHandler down, ClickHandler up) {
  (void)delay_ms; (void)up;
  s_lang[button_id] = down;
}

ActionBarLayer *action_bar_layer_create(void) { return calloc(1, sizeof(ActionBarLayer)); }
void action_bar_layer_destroy(ActionBarLayer *bar) {
  if (bar && bar->fenster) bar->fenster->leiste = NULL;
  free(bar);
}
void action_bar_layer_set_background_color(ActionBarLayer *bar, GColor color) { (void)bar; (void)color; }
void action_bar_layer_set_click_config_provider(ActionBarLayer *bar, ClickConfigProvider provider) { bar->tasten = provider; }
void action_bar_layer_set_icon(ActionBarLayer *bar, ButtonId button_id, const GBitmap *icon) { (void)bar; (void)button_id; (void)icon; }
void action_bar_layer_clear_icon(ActionBarLayer *bar, ButtonId button_id) { (void)bar; (void)button_id; }
void action_bar_layer_add_to_window(ActionBarLayer *bar, Window *window) { bar->fenster = window; window->leiste = bar; }
void action_bar_layer_remove_from_window(ActionBarLayer *bar) {
  if (bar->fenster) bar->fenster->leiste = NULL;
  bar->fenster = NULL;
}

// Die Tasten des obersten Fensters: die der Aktionsleiste, sonst die eigenen.
static void prv_tasten_holen(void) {
  memset(s_kurz, 0, sizeof(s_kurz));
  memset(s_lang, 0, sizeof(s_lang));
  Window *w = attrappe_oberstes_fenster();
  if (!w) return;
  ClickConfigProvider p = w->leiste ? w->leiste->tasten : w->tasten;
  if (p) p(NULL);
}
void attrappe_taste(ButtonId taste) {
  prv_tasten_holen();
  if (s_kurz[taste]) { s_kurz[taste](NULL, NULL); return; }
  // Zurueck ohne eigene Belegung nimmt das Fenster weg (window_stack.c).
  if (taste == BUTTON_ID_BACK) prv_pop(true);
}
void attrappe_taste_lang(ButtonId taste) {
  prv_tasten_holen();
  if (s_lang[taste]) s_lang[taste](NULL, NULL);
}

static void prv_zeichne(Layer *l) {
  if (l->versteckt) return;
  if (l->proc) l->proc(l, NULL);
  for (int i = 0; i < l->kinder_zahl; i++) prv_zeichne(l->kinder[i]);
}
void attrappe_zeichnen(void) {
  s_texte[0] = 0;
  Window *w = attrappe_oberstes_fenster();
  if (w) prv_zeichne(w->wurzel);
}
const char *attrappe_texte(void) { return s_texte; }

// --- Animation ---
struct Animation {
  const AnimationImplementation *impl;
  AnimationHandlers h;
  void *kontext;
  bool laeuft;
};
#define ANIM_MAX 4
static Animation *s_laufend[ANIM_MAX];
Animation *animation_create(void) { return calloc(1, sizeof(Animation)); }
bool animation_destroy(Animation *animation) {
  for (int i = 0; i < ANIM_MAX; i++) if (s_laufend[i] == animation) s_laufend[i] = NULL;
  free(animation);
  return true;
}
bool animation_set_implementation(Animation *a, const AnimationImplementation *impl) { a->impl = impl; return true; }
bool animation_set_duration(Animation *a, uint32_t duration_ms) { (void)a; (void)duration_ms; return true; }
bool animation_set_handlers(Animation *a, AnimationHandlers handlers, void *context) {
  a->h = handlers;
  a->kontext = context;
  return true;
}
bool animation_schedule(Animation *animation) {
  for (int i = 0; i < ANIM_MAX; i++) {
    if (!s_laufend[i]) { s_laufend[i] = animation; animation->laeuft = true; return true; }
  }
  return false;
}
// Wie im SDK: abgebrochen meldet stopped mit finished = false.
bool animation_unschedule(Animation *animation) {
  if (!animation || !animation->laeuft) return false;
  animation->laeuft = false;
  for (int i = 0; i < ANIM_MAX; i++) if (s_laufend[i] == animation) s_laufend[i] = NULL;
  if (animation->h.stopped) animation->h.stopped(animation, false, animation->kontext);
  return true;
}
int attrappe_animationen_beenden(void) {
  int n = 0;
  for (int i = 0; i < ANIM_MAX; i++) {
    Animation *a = s_laufend[i];
    if (!a) continue;
    s_laufend[i] = NULL;
    a->laeuft = false;
    if (a->impl && a->impl->update) a->impl->update(a, ANIMATION_NORMALIZED_MAX);
    if (a->h.stopped) a->h.stopped(a, true, a->kontext);
    n++;
  }
  return n;
}

// --- Dienste ---
void tick_timer_service_subscribe(TimeUnits tick_units, TickHandler handler) { (void)tick_units; (void)handler; }
void tick_timer_service_unsubscribe(void) {}
static int s_vibrationen;
void vibes_short_pulse(void) { s_vibrationen++; }
void vibes_double_pulse(void) { s_vibrationen++; }
int attrappe_vibrationen(void) { return s_vibrationen; }
bool attrappe_ruhezeit;
bool quiet_time_is_active(void) { return attrappe_ruhezeit; }
void clock_copy_time_string(char *buffer, uint8_t size) { snprintf(buffer, size, "08:00"); }

static char s_glance[64];
struct AppGlanceReloadSession { int dummy; };
AppGlanceResult app_glance_add_slice(AppGlanceReloadSession *session, AppGlanceSlice slice) {
  (void)session;
  snprintf(s_glance, sizeof(s_glance), "%s", slice.layout.subtitle_template_string);
  return APP_GLANCE_RESULT_SUCCESS;
}
void app_glance_reload(AppGlanceReloadCallback callback, void *context) {
  AppGlanceReloadSession s;
  if (callback) callback(&s, 1, context);
}
const char *attrappe_glance(void) { return s_glance; }

void (*attrappe_app_laeuft)(void);
void app_event_loop(void) {
  // pebbleos app.c: ohne Fenster endet die App, bevor ein Ereignis kommt.
  if (s_stapel_zahl == 0) { s_fenster_beim_ende = 0; return; }
  if (attrappe_app_laeuft) attrappe_app_laeuft();
  // Danach ist die App verlassen - von selbst oder weil die Uhr sie beendet
  // (langes Zurueck, eine andere App). pebbleos app.c prv_handle_deinit_event
  // sperrt dann neue Fenster und nimmt alle uebrigen ohne Animation weg,
  // MIT unload. Ohne das blieben die statischen Zeiger der Fenster ueber den
  // naechsten Start im selben Testprozess stehen.
  s_fenster_beim_ende = s_stapel_zahl;
  s_stapel_gesperrt = true;
  window_stack_pop_all(false);
  s_stapel_gesperrt = false;
}
int attrappe_fenster_beim_ende(void) { return s_fenster_beim_ende; }

void attrappe_ui_leeren(void) {
  s_stapel_zahl = 0;
  s_stapel_gesperrt = false;
  s_fenster_beim_ende = -1;
  s_vibrationen = 0;
  s_texte[0] = 0;
  s_glance[0] = 0;
  memset(s_laufend, 0, sizeof(s_laufend));
  attrappe_ruhezeit = false;
}
int attrappe_fenster_zahl(void) { return s_stapel_zahl; }
Window *attrappe_oberstes_fenster(void) { return s_stapel_zahl ? s_stapel[s_stapel_zahl - 1] : NULL; }
