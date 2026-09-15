#include "phone.h"
#include "plan.h"
#include "strings.h"

// Sechs Eintraege zu je 25 Byte plus Kopf. 256 laesst Luft.
#define INBOX_SIZE  256
#define OUTBOX_SIZE 64

static void (*s_observer)(void);
static AppTimer *s_retry;

static void prv_inbox(DictionaryIterator *iter, void *context) {
  Tuple *plan = dict_find(iter, MESSAGE_KEY_PLAN);
  if (!plan || plan->type != TUPLE_BYTE_ARRAY) return;
  if (plan_set_from_bytes(plan->value->data, plan->length) && s_observer) {
    s_observer();
  }
  // Der Plan ist da - jetzt weiss die Uhr, was heute ansteht, und kann es
  // dem Telefon fuer die Pins sagen.
  phone_send_today();
}

static void prv_retry_cb(void *data) {
  s_retry = NULL;
  phone_send_today();
}

void phone_send_today(void) {
  if (s_retry) {
    app_timer_cancel(s_retry);
    s_retry = NULL;
  }
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) {
    // Der Postausgang fasst genau EINE Nachricht. Kommt die Tagesmeldung zu
    // dicht hinter der Anfrage, faellt sie mit BUSY aus - und die Pins
    // blieben still auf dem Stand von gestern. Einmal nachfassen genuegt;
    // schlaegt auch das fehl, holt es der naechste Start nach.
    s_retry = app_timer_register(700, prv_retry_cb, NULL);
    return;
  }
  uint32_t due = 0, taken = 0;
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    if (plan_due_today(i)) due |= (1u << i);
    if (plan_taken(i)) taken |= (1u << i);
  }
  // Als KALENDERDATUM JJJJMMTT, nicht als Tagesnummer. Die Tagesnummer
  // zaehlt Tage seit der Epoche aus der Ortszeit; das Telefon kann daraus den
  // Kalendertag nicht sicher zurueckrechnen und landete bei positiver
  // Zeitzone einen Tag zu frueh - der Pin lag dann in der Vergangenheit.
  const time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  const int32_t ymd = (lt->tm_year + 1900) * 10000 + (lt->tm_mon + 1) * 100 + lt->tm_mday;
  dict_write_int32(out, MESSAGE_KEY_TODAY, ymd);
  dict_write_int32(out, MESSAGE_KEY_DUE, (int32_t)due);
  dict_write_int32(out, MESSAGE_KEY_TAKEN, (int32_t)taken);
  app_message_outbox_send();
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
