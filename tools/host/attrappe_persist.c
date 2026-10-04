// Persist im Speicher fuer die Host-Tests - so viele Faecher, wie die App
// belegt (plan.c 1..3, 8 und 9, prefs.c 4, remind.c 5..7), mit Luft.
#include <pebble.h>

#define FAECHER 16
static struct { bool da; uint8_t daten[256]; size_t laenge; } s_persist[FAECHER];

bool persist_exists(uint32_t key) { return key < FAECHER && s_persist[key].da; }
int persist_read_data(uint32_t key, void *buf, size_t size) {
  if (!persist_exists(key)) return -1;
  const size_t n = s_persist[key].laenge < size ? s_persist[key].laenge : size;
  memcpy(buf, s_persist[key].daten, n);
  return (int)n;
}
int32_t persist_read_int(uint32_t key) {
  int32_t v = 0;
  if (persist_exists(key)) memcpy(&v, s_persist[key].daten, sizeof(v));
  return v;
}
int persist_write_data(uint32_t key, const void *data, size_t size) {
  if (key >= FAECHER || size > sizeof(s_persist[key].daten)) return -1;
  s_persist[key].da = true;
  s_persist[key].laenge = size;
  memcpy(s_persist[key].daten, data, size);
  return (int)size;
}
int persist_write_int(uint32_t key, int32_t value) { return persist_write_data(key, &value, sizeof(value)); }
int persist_delete(uint32_t key) { if (key < FAECHER) s_persist[key].da = false; return 0; }
int persist_get_size(uint32_t key) { return persist_exists(key) ? (int)s_persist[key].laenge : E_DOES_NOT_EXIST; }
void attrappe_persist_leeren(void) { memset(s_persist, 0, sizeof(s_persist)); }
