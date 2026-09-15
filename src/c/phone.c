#include "phone.h"
#include "plan.h"
#include "strings.h"

// Sechs Eintraege zu je 25 Byte plus Kopf. 256 laesst Luft.
#define INBOX_SIZE  256
#define OUTBOX_SIZE 64

static void (*s_observer)(void);

static void prv_inbox(DictionaryIterator *iter, void *context) {
  Tuple *plan = dict_find(iter, MESSAGE_KEY_PLAN);
  if (!plan || plan->type != TUPLE_BYTE_ARRAY) return;
  if (plan_set_from_bytes(plan->value->data, plan->length) && s_observer) {
    s_observer();
  }
}

static void prv_ready(void *data) {
  // Einmal anfragen. Laeuft kein Telefon, bleibt es beim gespeicherten Plan -
  // die Uhr ist darauf nicht angewiesen.
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_int32(out, MESSAGE_KEY_REQUEST, 1);
  // Sprache der Uhr mitschicken: die Konfigseite wird auf dem TELEFON gebaut
  // und kann sie nicht von sich aus erfahren.
  dict_write_int32(out, MESSAGE_KEY_LANG, (int32_t)strings_language());
  app_message_outbox_send();
}

void phone_init(void) {
  app_message_register_inbox_received(prv_inbox);
  app_message_open(INBOX_SIZE, OUTBOX_SIZE);
  // Nicht sofort: pkjs braucht einen Moment, bis es zuhoert.
  app_timer_register(1500, prv_ready, NULL);
}

void phone_set_observer(void (*on_plan)(void)) {
  s_observer = on_plan;
}
