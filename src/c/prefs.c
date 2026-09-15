#include "prefs.h"

#define PERSIST_FX 4    // plan.c belegt 1..3 - siehe prefs.h

static bool s_fx = true;

void prefs_init(void) {
  // Ohne gespeicherten Wert: an. Wer nie etwas eingestellt hat, soll die
  // Animation sehen - sie ist der Teil, der die App freundlich macht.
  s_fx = persist_exists(PERSIST_FX) ? (persist_read_int(PERSIST_FX) != 0) : true;
}

bool prefs_fx(void) {
  return s_fx;
}

void prefs_set_fx(bool on) {
  if (s_fx == on) return;
  s_fx = on;
  persist_write_int(PERSIST_FX, on ? 1 : 0);
}
