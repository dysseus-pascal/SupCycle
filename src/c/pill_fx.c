#include "pill_fx.h"
#include "theme.h"

// Genommen-Animation im Stil der Timeline-Symbole: schwarzer Rahmen, weisse
// Füllung, Strich-Augen, geknickter Mund, alles mit derselben Strichstärke.
// Aufbau und Zeitverlauf folgen glass_fx.c aus Drinktervall.
//
// Ablauf in Promille der Gesamtdauer:
//   0..POP_END     Kapsel ploppt mit Überschwingen auf, lächelt
//   ..NOD_END      sie nickt zweimal — ein Lebenszeichen, sonst steht sie nur
//   ..SHRINK_END   Kapsel schrumpft ins Zentrum
//   ..1000         Strahlenkranz dort, wo die Kapsel war

#ifndef FX_MS
#define FX_MS 1400          // Gesamtdauer; Testbuilds können sie überschreiben
#endif
#define POP_END     110
#define NOD_END     520
#define SHRINK_END  700
#define RAYS         12

#define NOD_PX       2      // Nicken: Versatz nach oben/unten
#define NOD_MS      90

// Kapsel und Gesicht sind in Einheiten einer 72 Pixel BREITEN Kapsel
// beschrieben und werden über s_g.k auf die echte Breite skaliert. Die Kapsel
// ist höher als breit: 72 breit, 116 hoch, Radius 36.
#define BASE_W 72
#define HALF_W 36
#define HALF_H 58

static Layer *s_layer;
static Animation *s_anim;
static int32_t s_p;         // Fortschritt 0..1000
static GPoint s_anchor;
static int16_t s_width;
static PillFxDone s_done;

static struct {
  int32_t gw;
  int32_t k;                // Promille: Basis-Einheiten -> Pixel, inkl. Skalierung
  int16_t stroke;
  GPoint c;
} s_g;

static void prv_set_metrics(GPoint c, int32_t gw, int32_t scale) {
  s_g.c = c;
  s_g.gw = gw;
  s_g.k = scale * gw / BASE_W;
  s_g.stroke = (int16_t)((gw / 20) | 1);
}

static GPoint prv_gp(int32_t x, int32_t y) {
  return GPoint(s_g.c.x + (int16_t)(x * s_g.k / 1000),
                s_g.c.y + (int16_t)(y * s_g.k / 1000));
}

static int16_t prv_len(int32_t units) {
  return (int16_t)(units * s_g.k / 1000);
}

// Jeder Strich zweimal: erst der breite weisse Saum, dann schwarz darüber. So
// bleibt er auch auf der roten Hälfte lesbar.
static void prv_pen(GContext *ctx, bool halo) {
  graphics_context_set_stroke_color(ctx, halo ? GColorWhite : GColorBlack);
  graphics_context_set_stroke_width(ctx, halo ? s_g.stroke + 2 : s_g.stroke);
}

static void prv_line(GContext *ctx, GPoint a, GPoint b) {
  prv_pen(ctx, true);
  graphics_draw_line(ctx, a, b);
  prv_pen(ctx, false);
  graphics_draw_line(ctx, a, b);
}

// Offene Polylinie in einem Zug, damit am Knick keine Naht entsteht
static void prv_polyline(GContext *ctx, GPoint *points, uint32_t n) {
  const GPathInfo info = { .num_points = n, .points = points };
  GPath *path = gpath_create(&info);
  if (!path) return;
  prv_pen(ctx, true);
  gpath_draw_outline_open(ctx, path);
  prv_pen(ctx, false);
  gpath_draw_outline_open(ctx, path);
  gpath_destroy(path);
}

// Gesicht auf der unteren, weissen Hälfte — leicht nach links versetzt
// (Seitenblick), wie beim Glas.
static void prv_face(GContext *ctx) {
  const int32_t fy = 22;                      // Mitte der unteren Hälfte
  const int32_t eyes[2] = { -13, 7 };
  for (int i = 0; i < 2; i++) {
    prv_line(ctx, prv_gp(eyes[i], fy - 9), prv_gp(eyes[i], fy - 2));
  }
  GPoint mouth[3] = { prv_gp(-14, fy + 6), prv_gp(-3, fy + 12), prv_gp(8, fy + 6) };
  prv_polyline(ctx, mouth, 3);
}

// Die Kapsel: ein Stadion-Umriss, obere Hälfte rot, untere weiss.
//
// Ohne Zuschneiden gebaut - die SDK bietet dafür nichts Öffentliches. Statt
// dessen: erst das Ganze weiss, dann die obere Hälfte als Rechteck mit NUR
// oben runden Ecken darüber. Das trifft die Kapselform genau, weil der Radius
// gleich der halben Breite ist.
static void prv_draw_pill(GContext *ctx) {
  const int16_t hw = prv_len(HALF_W);
  const int16_t hh = prv_len(HALF_H);
  if (hw < 2 || hh < 2) return;

  const GRect full = GRect(s_g.c.x - hw, s_g.c.y - hh, hw * 2, hh * 2);
  const GRect top = GRect(full.origin.x, full.origin.y, full.size.w, hh);

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, full, hw, GCornersAll);

  graphics_context_set_fill_color(ctx, SC_COLOR_PILL);
  graphics_fill_rect(ctx, top, hw, GCornersTop);

  // Trennlinie und Rahmen
  prv_pen(ctx, false);
  graphics_draw_line(ctx, GPoint(full.origin.x, s_g.c.y),
                          GPoint(full.origin.x + full.size.w - 1, s_g.c.y));
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, s_g.stroke);
  graphics_draw_round_rect(ctx, full, hw);

  prv_face(ctx);
}

// Strahlenkranz: zwölf Striche nach aussen, abwechselnd lang und kurz, dazu
// ein schrumpfender Funken in der Mitte. Übernommen aus glass_fx.c — dieselbe
// Geste, damit die Apps sich gleich anfühlen. u = 0..1000
static void prv_draw_burst(GContext *ctx, int32_t u) {
  const int32_t g = u < 500 ? u * 2 : (1000 - u) * 2;               // 0..1000..0
  const int32_t ge = 1000 - (1000 - g) * (1000 - g) / 1000;         // ease-out
  const int32_t r1 = 6 + 34 * u / 1000;
  const int32_t len = s_g.gw * 42 / 100 * ge / 1000;
  for (int k = 0; k < RAYS; k++) {
    const int32_t a = k * TRIG_MAX_ANGLE / RAYS + u * (TRIG_MAX_ANGLE / 24) / 1000;
    const int32_t l = (k % 2) ? len * 55 / 100 : len;
    const int32_t cs = cos_lookup(a), sn = sin_lookup(a);
    const GPoint p1 = GPoint(s_g.c.x + r1 * cs / TRIG_MAX_RATIO,
                             s_g.c.y + r1 * sn / TRIG_MAX_RATIO);
    const GPoint p2 = GPoint(s_g.c.x + (r1 + l) * cs / TRIG_MAX_RATIO,
                             s_g.c.y + (r1 + l) * sn / TRIG_MAX_RATIO);
    prv_line(ctx, p1, p2);
  }
  const int32_t ro = 2 + (s_g.gw * 20 / 100) * (1000 - u) / 1000;
  const int32_t ri = 1 + (s_g.gw * 7 / 100) * (1000 - u) / 1000;
  GPoint sp[8];
  for (int i = 0; i < 8; i++) {
    const int32_t r = (i % 2) ? ri : ro;
    const int32_t a = -TRIG_MAX_ANGLE / 4 + i * TRIG_MAX_ANGLE / 8;
    sp[i] = GPoint(s_g.c.x + r * cos_lookup(a) / TRIG_MAX_RATIO,
                   s_g.c.y + r * sin_lookup(a) / TRIG_MAX_RATIO);
  }
  const GPathInfo sinfo = { .num_points = 8, .points = sp };
  GPath *star = gpath_create(&sinfo);
  if (!star) return;
  graphics_context_set_fill_color(ctx, GColorWhite);
  gpath_draw_filled(ctx, star);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, s_g.stroke > 3 ? 2 : 1);
  gpath_draw_outline(ctx, star);
  gpath_destroy(star);
}

// smoothstep, Ein- und Ausgabe in Promille
static int32_t prv_smooth(int32_t f) {
  return f * f * (3000 - 2 * f) / 1000000;
}

static void prv_draw(Layer *layer, GContext *ctx) {
  GPoint c = s_anchor;
  int32_t scale = 1000;

  if (s_p >= POP_END && s_p < NOD_END) {
    // Nicken: ein Lebenszeichen, sonst stünde sie nur da. Nach oben und unten,
    // nicht seitlich - seitlich sähe aus wie das Schütteln des Glases beim
    // Leeren, und hier wird nichts geleert.
    const int32_t ms = (s_p - POP_END) * FX_MS / 1000;
    c.y += ((ms / NOD_MS) % 2) ? NOD_PX : -NOD_PX;
  }

  if (s_p < POP_END) {
    // Aufploppen mit Überschwingen: 0 -> 1150 -> 1000
    const int32_t t = s_p * 1000 / POP_END;
    scale = t < 600 ? 1150 * prv_smooth(t * 1000 / 600) / 1000
                    : 1150 - 150 * (t - 600) / 400;
  } else if (s_p >= NOD_END && s_p < SHRINK_END) {
    const int32_t t = (s_p - NOD_END) * 1000 / (SHRINK_END - NOD_END);
    scale = 1000 - t * t / 1000;                                    // beschleunigt
  }
  prv_set_metrics(c, s_width, scale);

  if (s_p < SHRINK_END) {
    // Das letzte Zwergenexemplar sparen wir uns - unter dieser Grösse ist es
    // nur noch ein Fleck, und der Strahlenkranz kommt ohnehin gleich.
    if (scale > 60) prv_draw_pill(ctx);
  } else {
    prv_draw_burst(ctx, (s_p - SHRINK_END) * 1000 / (1000 - SHRINK_END));
  }
}

void pill_fx_draw_still(GContext *ctx, GPoint center, int16_t width) {
  prv_set_metrics(center, width, 1000);
  prv_draw_pill(ctx);
}

static void prv_update(Animation *animation, const AnimationProgress progress) {
  s_p = (int32_t)progress * 1000 / ANIMATION_NORMALIZED_MAX;
  layer_mark_dirty(s_layer);
}

static const AnimationImplementation s_impl = { .update = prv_update };

static void prv_stopped(Animation *animation, bool finished, void *context) {
  // Das SDK gibt beendete Animationen nicht selbst frei
  animation_destroy(animation);
  s_anim = NULL;
  if (s_layer) layer_set_hidden(s_layer, true);
  PillFxDone done = s_done;
  s_done = NULL;
  if (finished && done) done();
}

void pill_fx_init(Layer *parent) {
  s_layer = layer_create(layer_get_bounds(parent));
  layer_set_update_proc(s_layer, prv_draw);
  layer_set_hidden(s_layer, true);
  layer_add_child(parent, s_layer);
}

void pill_fx_deinit(void) {
  s_done = NULL;
  if (s_anim) {
    animation_unschedule(s_anim);
    s_anim = NULL;
  }
  if (s_layer) {
    layer_destroy(s_layer);
    s_layer = NULL;
  }
}

void pill_fx_play(GPoint anchor, int16_t width, PillFxDone done) {
  if (s_anim || !s_layer) return;
  s_anchor = anchor;
  s_width = width;
  s_done = done;
  s_p = 0;

  s_anim = animation_create();
  if (!s_anim) {
    // Kein Speicher für die Animation: dann eben ohne. Der Aufrufer wartet
    // sonst ewig auf ein Ende, das nie kommt.
    s_done = NULL;
    if (done) done();
    return;
  }
  animation_set_implementation(s_anim, &s_impl);
  animation_set_duration(s_anim, FX_MS);
  animation_set_handlers(s_anim, (AnimationHandlers) { .stopped = prv_stopped }, NULL);
  layer_set_hidden(s_layer, false);
  animation_schedule(s_anim);
}
