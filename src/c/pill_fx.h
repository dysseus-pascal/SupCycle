#pragma once
#include <pebble.h>

typedef void (*PillFxDone)(void);

// Overlay fuer die Genommen-Animation: eine Kapsel ploppt auf, laechelt,
// nickt kurz, schrumpft ins Zentrum und zerplatzt in einem Strahlenkranz.
// Der genaue Ablauf steht am Kopf von pill_fx.c.
//
// Aufbau und Zeitverlauf folgen glass_fx.c aus Drinktervall - dasselbe
// Strichbild, derselbe Strahlenkranz, dieselbe Mechanik. Was fehlt, ist das
// Leeren: eine Kapsel wird genommen, nicht ausgetrunken.
// Legt den Overlay über `parent`. Ein bereits vorhandener gehört einem
// anderen Fenster und wird dabei abgeräumt - es gibt nur einen.
void pill_fx_init(Layer *parent);

// Räumt den Overlay weg, ABER nur wenn er zu `parent` gehört. Liegt beim
// Entladen eines Fensters schon der Overlay eines anderen da, wäre das
// Wegräumen ein Griff in fremden Besitz - und genau daran fror die App ein.
void pill_fx_deinit(Layer *parent);

// Animation starten; `anchor` ist die Kapselmitte in Koordinaten des
// Parent-Layers, `width` ihre Breite. `done` wird nach dem regulaeren Ende
// gerufen (nicht bei Abbruch durch pill_fx_deinit), bei fehlendem Speicher
// auch sofort. Laeuft schon eine Animation, passiert nichts.
// Rückgabe: true, wenn die Animation läuft. Bei false ist `done` bereits
// gerufen worden (oder es lief schon eine) - der Aufrufer darf NICHT auf ein
// Ende warten, das nicht kommt.
bool pill_fx_play(GPoint anchor, int16_t width, PillFxDone done);

// Stehende Kapsel im selben Stil an beliebiger Stelle zeichnen, etwa in der
// Seitenleiste. Unabhaengig vom Overlay-Layer.
void pill_fx_draw_still(GContext *ctx, GPoint center, int16_t width);
