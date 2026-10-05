// Nur fuer die Host-Tests: Fenster, Ebenen, Tasten, Zeichnen, Animation -
// so viel vom SDK, dass main_window.c, reminder_window.c, pill_fx.c und
// supcycle.c uebersetzen und laufen. Gezeichnet wird nichts; die Zeichen-
// funktionen laufen aber durch, damit ein Fehler in der Beschriftung (etwa
// ein Format ohne Zahl) im Test auffaellt und nicht erst auf der Uhr.
//
// Verhaelt sich, wo es fuer den Ablauf zaehlt, wie die Uhr (pebbleos
// applib/ui, applib/app.c): ein Fensterstapel mit load/appear/disappear/
// unload, Zurueck ohne eigene Belegung nimmt das oberste Fenster weg, und eine
// App ohne Fenster endet.
#pragma once

// Emery: 200 x 228, rechteckig, farbig.
#define PBL_DISPLAY_WIDTH 200
#define PBL_DISPLAY_HEIGHT 228
#define PBL_IF_ROUND_ELSE(rund, eckig) (eckig)
#define PBL_IF_COLOR_ELSE(farbig, sw) (farbig)
#define ACTION_BAR_WIDTH 34

typedef struct { int16_t x, y; } GPoint;
typedef struct { int16_t w, h; } GSize;
typedef struct { GPoint origin; GSize size; } GRect;
#define GPoint(x, y) ((GPoint){ (int16_t)(x), (int16_t)(y) })
#define GSize(w, h) ((GSize){ (int16_t)(w), (int16_t)(h) })
#define GRect(x, y, w, h) ((GRect){ { (int16_t)(x), (int16_t)(y) }, { (int16_t)(w), (int16_t)(h) } })

typedef union { uint8_t argb; } GColor8;
typedef GColor8 GColor;
#define GColorBlack ((GColor8){ .argb = 0xC0 })
#define GColorWhite ((GColor8){ .argb = 0xFF })
#define GColorRed ((GColor8){ .argb = 0xF0 })
#define GColorMidnightGreen ((GColor8){ .argb = 0xC5 })
#define GColorTiffanyBlue ((GColor8){ .argb = 0xDA })

typedef struct GContext GContext;
typedef struct AttrappeSchrift *GFont;
typedef struct GTextAttributes GTextAttributes;
typedef enum { GTextOverflowModeWordWrap, GTextOverflowModeTrailingEllipsis, GTextOverflowModeFill } GTextOverflowMode;
typedef enum { GTextAlignmentLeft, GTextAlignmentCenter, GTextAlignmentRight } GTextAlignment;
typedef enum { GCornerNone = 0, GCornersTop = 3, GCornersAll = 15 } GCornerMask;

#define FONT_KEY_GOTHIC_14 "RESOURCE_ID_GOTHIC_14"
#define FONT_KEY_GOTHIC_18 "RESOURCE_ID_GOTHIC_18"
#define FONT_KEY_GOTHIC_18_BOLD "RESOURCE_ID_GOTHIC_18_BOLD"
#define FONT_KEY_GOTHIC_24_BOLD "RESOURCE_ID_GOTHIC_24_BOLD"
#define FONT_KEY_LECO_20_BOLD_NUMBERS "RESOURCE_ID_LECO_20_BOLD_NUMBERS"
#define FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM "RESOURCE_ID_LECO_26_BOLD_NUMBERS_AM_PM"
#define FONT_KEY_LECO_32_BOLD_NUMBERS "RESOURCE_ID_LECO_32_BOLD_NUMBERS"
GFont fonts_get_system_font(const char *font_key);

void graphics_context_set_text_color(GContext *ctx, GColor color);
void graphics_context_set_fill_color(GContext *ctx, GColor color);
void graphics_context_set_stroke_color(GContext *ctx, GColor color);
void graphics_context_set_stroke_width(GContext *ctx, uint8_t width);
void graphics_draw_text(GContext *ctx, const char *text, GFont font, GRect box,
                        GTextOverflowMode overflow, GTextAlignment alignment, GTextAttributes *attributes);
GSize graphics_text_layout_get_content_size(const char *text, GFont font, GRect box,
                                            GTextOverflowMode overflow, GTextAlignment alignment);
void graphics_fill_rect(GContext *ctx, GRect rect, uint16_t corner_radius, GCornerMask corner_mask);
void graphics_draw_line(GContext *ctx, GPoint p0, GPoint p1);
void graphics_draw_round_rect(GContext *ctx, GRect rect, uint16_t radius);

typedef struct { uint32_t num_points; GPoint *points; } GPathInfo;
typedef struct GPath GPath;
GPath *gpath_create(const GPathInfo *init);
void gpath_destroy(GPath *path);
void gpath_draw_filled(GContext *ctx, GPath *path);
void gpath_draw_outline(GContext *ctx, GPath *path);
void gpath_draw_outline_open(GContext *ctx, GPath *path);

#define TRIG_MAX_ANGLE 0x10000
#define TRIG_MAX_RATIO 0xffff
int32_t sin_lookup(int32_t angle);
int32_t cos_lookup(int32_t angle);

typedef struct GBitmap GBitmap;
#define RESOURCE_ID_ICON_CHECK 1
#define RESOURCE_ID_ICON_SNOOZE 2
GBitmap *gbitmap_create_with_resource(uint32_t resource_id);
void gbitmap_destroy(GBitmap *bitmap);

// --- Ebenen und Fenster ---
typedef struct Layer Layer;
typedef void (*LayerUpdateProc)(Layer *layer, GContext *ctx);
Layer *layer_create(GRect frame);
void layer_destroy(Layer *layer);
GRect layer_get_bounds(const Layer *layer);
void layer_set_update_proc(Layer *layer, LayerUpdateProc update_proc);
void layer_add_child(Layer *parent, Layer *child);
void layer_mark_dirty(Layer *layer);
void layer_set_hidden(Layer *layer, bool hidden);

typedef enum { BUTTON_ID_BACK = 0, BUTTON_ID_UP, BUTTON_ID_SELECT, BUTTON_ID_DOWN, NUM_BUTTONS } ButtonId;
typedef void *ClickRecognizerRef;
typedef void (*ClickHandler)(ClickRecognizerRef recognizer, void *context);
typedef void (*ClickConfigProvider)(void *context);

typedef struct Window Window;
typedef void (*WindowHandler)(Window *window);
typedef struct { WindowHandler load, appear, disappear, unload; } WindowHandlers;
Window *window_create(void);
void window_destroy(Window *window);
void window_set_background_color(Window *window, GColor color);
void window_set_window_handlers(Window *window, WindowHandlers handlers);
void window_set_click_config_provider(Window *window, ClickConfigProvider provider);
Layer *window_get_root_layer(const Window *window);
void window_stack_push(Window *window, bool animated);
void window_stack_pop_all(bool animated);
void window_single_click_subscribe(ButtonId button_id, ClickHandler handler);
void window_long_click_subscribe(ButtonId button_id, uint16_t delay_ms, ClickHandler down, ClickHandler up);

typedef struct ActionBarLayer ActionBarLayer;
ActionBarLayer *action_bar_layer_create(void);
void action_bar_layer_destroy(ActionBarLayer *bar);
void action_bar_layer_set_background_color(ActionBarLayer *bar, GColor color);
void action_bar_layer_set_click_config_provider(ActionBarLayer *bar, ClickConfigProvider provider);
void action_bar_layer_set_icon(ActionBarLayer *bar, ButtonId button_id, const GBitmap *icon);
void action_bar_layer_clear_icon(ActionBarLayer *bar, ButtonId button_id);
void action_bar_layer_add_to_window(ActionBarLayer *bar, Window *window);
void action_bar_layer_remove_from_window(ActionBarLayer *bar);

// --- Animation ---
typedef struct Animation Animation;
typedef int32_t AnimationProgress;
#define ANIMATION_NORMALIZED_MAX 65535
typedef void (*AnimationUpdateImplementation)(Animation *animation, const AnimationProgress progress);
typedef struct { void (*setup)(Animation *animation); AnimationUpdateImplementation update;
                 void (*teardown)(Animation *animation); } AnimationImplementation;
typedef void (*AnimationStartedHandler)(Animation *animation, void *context);
typedef void (*AnimationStoppedHandler)(Animation *animation, bool finished, void *context);
typedef struct { AnimationStartedHandler started; AnimationStoppedHandler stopped; } AnimationHandlers;
Animation *animation_create(void);
bool animation_destroy(Animation *animation);
bool animation_set_implementation(Animation *animation, const AnimationImplementation *implementation);
bool animation_set_duration(Animation *animation, uint32_t duration_ms);
bool animation_set_handlers(Animation *animation, AnimationHandlers handlers, void *context);
bool animation_schedule(Animation *animation);
bool animation_unschedule(Animation *animation);

// --- Dienste ---
typedef enum { SECOND_UNIT = 1, MINUTE_UNIT = 2, HOUR_UNIT = 4, DAY_UNIT = 8 } TimeUnits;
typedef void (*TickHandler)(struct tm *tick_time, TimeUnits units_changed);
void tick_timer_service_subscribe(TimeUnits tick_units, TickHandler handler);
void tick_timer_service_unsubscribe(void);
void vibes_short_pulse(void);
void vibes_double_pulse(void);
bool quiet_time_is_active(void);
void clock_copy_time_string(char *buffer, uint8_t size);

typedef struct AppGlanceReloadSession AppGlanceReloadSession;
typedef void (*AppGlanceReloadCallback)(AppGlanceReloadSession *session, size_t limit, void *context);
#define APP_GLANCE_SLICE_DEFAULT_ICON 0
#define APP_GLANCE_SLICE_NO_EXPIRATION ((time_t)0)
typedef struct {
  struct { uint32_t icon; const char *subtitle_template_string; } layout;
  time_t expiration_time;
} AppGlanceSlice;
typedef enum { APP_GLANCE_RESULT_SUCCESS = 0 } AppGlanceResult;
AppGlanceResult app_glance_add_slice(AppGlanceReloadSession *session, AppGlanceSlice slice);
void app_glance_reload(AppGlanceReloadCallback callback, void *context);

void app_event_loop(void);

// --- Was der Test damit tut ---
// Laeuft, waehrend die App in app_event_loop steht - aber nur, solange sie
// ein Fenster hat (pebbleos beendet eine App ohne Fenster sofort).
extern void (*attrappe_app_laeuft)(void);
void attrappe_ui_leeren(void);
int attrappe_fenster_zahl(void);
// Wie viele Fenster standen, als die App verlassen wurde (vor dem Abbau);
// -1 = sie lief noch nicht zu Ende.
int attrappe_fenster_beim_ende(void);
Window *attrappe_oberstes_fenster(void);
// Eine Taste auf dem obersten Fenster druecken (kurz oder lang).
void attrappe_taste(ButtonId taste);
void attrappe_taste_lang(ButtonId taste);
// Das oberste Fenster einmal zeichnen: alle sichtbaren Ebenen.
void attrappe_zeichnen(void);
// Was zuletzt mit graphics_draw_text auf den Schirm kam, durch '|' getrennt.
const char *attrappe_texte(void);
int attrappe_vibrationen(void);
extern bool attrappe_ruhezeit;
// Laufende Animationen zu Ende spielen (finished = true).
int attrappe_animationen_beenden(void);
// Was der App-Glance zuletzt zeigte.
const char *attrappe_glance(void);
