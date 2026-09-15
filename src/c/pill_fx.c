#include "pill_fx.h"
#include "theme.h"

// Genommen-Animation im Stil der Timeline-Symbole: schwarzer Rahmen, weisse
// Füllung, Strich-Augen, geknickter Mund, alles mit derselben Strichstärke.
// Aufbau und Zeitverlauf folgen glass_fx.c aus Drinktervall.
//
// Ablauf in Promille der Gesamtdauer:
//   0..POP_END     Kapsel ploppt mit Überschwingen auf, lächelt
//   ..HOLD_END     kurz still
//   ..SHAKE_END    sie wird geschüttelt und verzieht das Gesicht
//   ..STILL_END    das Schütteln hört auf, die Miene BLEIBT
//   ..SHRINK_END   Kapsel schrumpft ins Zentrum
//   ..1000         Strahlenkranz dort, wo die Kapsel war
//
// Die verzogene Miene bleibt bis zum Schluss. Vorher lächelte die Kapsel
// wieder, sobald das Schütteln aufhörte - das sah aus, als sei nichts
// gewesen, und nahm dem Schütteln die Pointe.

#ifndef FX_MS
#define FX_MS 1400          // Gesamtdauer; Testbuilds können sie überschreiben
#endif
#define POP_END     100
#define HOLD_END    170
#define SHAKE_END   560
#define STILL_END   640
#define SHRINK_END  780
#define RAYS         12

#define SHAKE_PX     3      // Schütteln: Versatz links/rechts
#define SHAKE_MS    55      // ... und Wechsel alle 55 ms

// Kapsel und Gesicht sind in Einheiten einer 72 Pixel BREITEN Kapsel
// beschrieben und werden über s_g.k auf die echte Breite skaliert.
//
// Sie ist deutlich höher als breit: 72 zu 156, Radius 36. Eine gedrungene
// Kapsel sieht aus wie eine Tablette; erst dieses Verhältnis liest sich als
// Kapsel. Wer sie schlanker macht, muss die Spielbreite in reminder_window.c
// mitziehen, sonst wächst sie aus dem Schirm: die Höhe ist gut das
// Zweifache der Breite, beim Aufploppen kurz das 2,5-fache.
#define BASE_W 72
#define HALF_W 36
#define HALF_H 78

static Layer *s_layer;
static Layer *s_parent;     //< wem der Overlay gerade gehört
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

typedef enum { FaceSmile, FaceShaken } Face;

// Gesicht auf der OBEREN, roten Hälfte — leicht nach links versetzt
// (Seitenblick), wie beim Glas.
//
// Auf Rot statt auf Weiss: jeder Strich wird erst mit weissem Saum und dann
// schwarz gezogen (prv_pen), er hebt sich also auch dort ab. Und oben sitzt
// das Gesicht dort, wo man bei einem Gegenüber hinsieht.
// Die Maße unten sind gegenüber der ersten Fassung um gut ein Drittel
// gewachsen, gestreckt um die Mitte des Gesichts (x = -3), nicht um die der
// Kapsel: sonst wäre der Seitenblick mitgewandert. Nach oben ist Luft bis
// y = -52, dort misst der Stadion-Umriss noch 34 Pixel halbe Breite.
static void prv_face(GContext *ctx, Face face) {
  const int32_t fy = -40;                     // Mitte der oberen Hälfte
  const int32_t eyes[2] = { -17, 10 };

  for (int i = 0; i < 2; i++) {
    const int32_t x = eyes[i];
    if (face == FaceShaken) {
      // Zusammengekniffen: das linke Auge ein ">", das rechte ein "<".
      // Die Spitzen zeigen zueinander — so liest sich das Paar als "><".
      const int32_t dir = (i == 0) ? 1 : -1;
      GPoint eye[3] = {
        prv_gp(x - 5 * dir, fy - 12),
        prv_gp(x + 4 * dir, fy - 7),
        prv_gp(x - 5 * dir, fy - 1),
      };
      prv_polyline(ctx, eye, 3);
    } else {
      prv_line(ctx, prv_gp(x, fy - 12), prv_gp(x, fy - 3));
    }
  }

  if (face == FaceShaken) {
    // Gezackter Mund: fünf Punkte auf und ab. Der Knick allein sagt schon
    // "unangenehm", die Zacken machen daraus ein Zähneknirschen.
    GPoint mouth[5] = {
      prv_gp(-18, fy + 12), prv_gp(-10, fy + 5), prv_gp(-3, fy + 15),
      prv_gp(5, fy + 5), prv_gp(13, fy + 12),
    };
    prv_polyline(ctx, mouth, 5);
  } else {
    GPoint mouth[3] = { prv_gp(-18, fy + 8), prv_gp(-3, fy + 16), prv_gp(12, fy + 8) };
    prv_polyline(ctx, mouth, 3);
  }
}

// Die Kapsel: ein Stadion-Umriss, obere Hälfte rot, untere weiss.
//
// Ohne Zuschneiden gebaut - die SDK bietet dafür nichts Öffentliches. Statt
// dessen: erst das Ganze weiss, dann die obere Hälfte als Rechteck mit NUR
// oben runden Ecken darüber. Das trifft die Kapselform genau, weil der Radius
// gleich der halben Breite ist.
static void prv_draw_pill(GContext *ctx, Face face) {
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

  prv_face(ctx, face);
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

  if (s_p >= HOLD_END && s_p < SHAKE_END) {
    // Geschüttelt: seitlich, damit es sich vom Aufploppen unterscheidet.
    const int32_t ms = (s_p - HOLD_END) * FX_MS / 1000;
    c.x += ((ms / SHAKE_MS) % 2) ? SHAKE_PX : -SHAKE_PX;
  }

  if (s_p < POP_END) {
    // Aufploppen mit Überschwingen: 0 -> 1150 -> 1000
    const int32_t t = s_p * 1000 / POP_END;
    scale = t < 600 ? 1150 * prv_smooth(t * 1000 / 600) / 1000
                    : 1150 - 150 * (t - 600) / 400;
  } else if (s_p >= STILL_END && s_p < SHRINK_END) {
    const int32_t t = (s_p - STILL_END) * 1000 / (SHRINK_END - STILL_END);
    scale = 1000 - t * t / 1000;                                    // beschleunigt
  }
  prv_set_metrics(c, s_width, scale);

  if (s_p < SHRINK_END) {
    // Das letzte Zwergenexemplar sparen wir uns - unter dieser Grösse ist es
    // nur noch ein Fleck, und der Strahlenkranz kommt ohnehin gleich.
    // Einmal geschüttelt, bleibt die Miene - auch während die Kapsel
    // schrumpft. Nur die Bewegung hört bei SHAKE_END auf, nicht der Ausdruck.
    const bool shaken = (s_p >= HOLD_END);
    if (scale > 60) prv_draw_pill(ctx, shaken ? FaceShaken : FaceSmile);
  } else {
    prv_draw_burst(ctx, (s_p - SHRINK_END) * 1000 / (1000 - SHRINK_END));
  }
}

void pill_fx_draw_still(GContext *ctx, GPoint center, int16_t width) {
  prv_set_metrics(center, width, 1000);
  prv_draw_pill(ctx, FaceSmile);
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
  if (!parent) return;
  // Den alten zuerst weg: sonst bliebe er als Waise im Baum des anderen
  // Fensters hängen und leckte bis zum App-Ende.
  if (s_layer) {
    layer_destroy(s_layer);
    s_layer = NULL;
  }
  s_parent = parent;
  s_layer = layer_create(layer_get_bounds(parent));
  layer_set_update_proc(s_layer, prv_draw);
  layer_set_hidden(s_layer, true);
  layer_add_child(parent, s_layer);
}

void pill_fx_deinit(Layer *parent) {
  // Gehört der Overlay inzwischen einem anderen Fenster, ist hier nichts zu
  // tun. Sonst risse das entladende Fenster dem sichtbaren den Boden weg.
  if (parent && s_parent && s_parent != parent) return;
  s_parent = NULL;
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

bool pill_fx_play(GPoint anchor, int16_t width, PillFxDone done) {
  // Es läuft schon eine: der Aufrufer bekommt kein zweites Ende versprochen.
  if (s_anim) return false;
  if (!s_layer) {
    // Kein Overlay - dann eben ohne Animation, aber MIT Meldung. Schwiege
    // ich hier, wartet der Aufrufer ewig auf ein Ende, das nie kommt, und
    // die App steht.
    if (done) done();
    return false;
  }
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
    return false;
  }
  animation_set_implementation(s_anim, &s_impl);
  animation_set_duration(s_anim, FX_MS);
  animation_set_handlers(s_anim, (AnimationHandlers) { .stopped = prv_stopped }, NULL);
  layer_set_hidden(s_layer, false);
  animation_schedule(s_anim);
  return true;
}
