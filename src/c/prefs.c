#include "prefs.h"

#define PERSIST_FX 4    // plan.c belegt 1..3, 8 und 9, remind.c 5..7 und 10 - siehe prefs.h

static bool s_fx = true;

void prefs_init(void) {
  // Ohne gespeicherten Wert: an. Wer nie etwas eingestellt hat, soll die
  // Animation sehen - sie ist der Teil, der die App freundlich macht.
  // NUR EINE ZAHL (4 Byte) IST EIN SCHALTER. SupCycle 0.14.0 schrieb die
  // Hakenzeiten (24 Byte) in dieses Fach; deren erste Zeit las sich als
  // "an/aus" - ohne Haken auf Platz 1 ging die Animation still aus. Steht dort
  // noch so ein Block, gilt die Voreinstellung (plan_init raeumt ihn weg).
  s_fx = persist_get_size(PERSIST_FX) == (int)sizeof(int32_t) ? (persist_read_int(PERSIST_FX) != 0) : true;
}

bool prefs_fx(void) {
  return s_fx;
}

void prefs_set_fx(bool on) {
  if (s_fx == on) return;
  s_fx = on;
  persist_write_int(PERSIST_FX, on ? 1 : 0);
}
