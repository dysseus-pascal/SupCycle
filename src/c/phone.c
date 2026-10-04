#include "phone.h"
#include "plan.h"
#include "prefs.h"
#include "strings.h"

// Sechs Eintraege zu je 25 Byte plus Kopf. 256 laesst Luft - auch fuer den
// Befehl, Haken zurueckzunehmen (Tag, Maske, 24 Byte Zeiten).
#define INBOX_SIZE  256
// Der Postausgang traegt seit 0.10.0 auch die Namen: sechs mal sechzehn
// Byte plus Trenner sind gut hundert, dazu drei Zahlen und die Koepfe.
// 64 reichten fuer die Zahlen allein und fuer nichts sonst.
// Der Plan faehrt in jeder Meldung mit (156 Byte), dazu die Namen und der
// Tagesstand - 256 reichten dafuer nicht mehr. Der groesste Fall - voller
// Plan mit 15-Byte-Namen, Hakenzeiten, Startanfrage mit Sprache und die
// Frage nach der Zeit - braucht 375 Byte (nachgezaehlt in
// tools/phone_host_test.c).
#define OUTBOX_SIZE 512

// Alle sechs Plaetze, durch Zeilenumbruch getrennt - auch die leeren.
// DIE STELLE IST DIE AUSSAGE: die Bitmasken zaehlen Plaetze, nicht
// Eintraege. Wer die leeren weglaesst, verschiebt jeden Namen dahinter.
#define NAMEN_BYTES (SC_MAX_ITEMS * (SC_NAME_LEN + 1))

static void (*s_observer)(void);
static AppTimer *s_retry;
// Der Tagesstand kam nicht an - bei der naechsten Verbindung nachholen.
static bool s_unsent;
// Die Startanfrage steht noch aus - auch ein Nachfassen nach BUSY muss sie
// tragen, sonst erfaehrt das Telefon die Sprache nie.
//
// ERLEDIGT IST SIE ERST, WENN DAS TELEFON SIE BESTAETIGT (prv_outbox_sent).
// Bis 0.14.0 galt sie schon beim Abschicken als erledigt. Hoerte pkjs noch
// nicht zu (NACK), ging beim Nachholen nur der Plan der Uhr ohne REQUEST
// hinaus - bei einer frischen Uhr ein leerer -, und die Telefonseite legte ihn
// ueber ihren gespeicherten Plan: der Plan war weg (im Emulator nachgestellt,
// 03.10.2026).
static bool s_anfrage;
// Wie oft die Startanfrage nach einem Fehlschlag schon nachgefasst hat. Ein
// Telefon, dessen pkjs gerade erst anlaeuft, antwortet Sekunden spaeter -
// ohne Nachfassen stuende eine frische Uhr bis zum naechsten Start ohne Plan.
static uint8_t s_nachgefasst;
#define ANFRAGE_NACHFASSEN_MAX 4
#define ANFRAGE_NACHFASSEN_MS  3000

// Eine Zahl aus einem Tupel, unabhaengig davon, wie breit sie ankam.
// Ein int32 von einem Ein-Byte-Tupel zu lesen ergaebe Unsinn, und die
// Telefonseite bestimmt die Breite nicht - das tut die Bibliothek.
static int32_t prv_tuple_int(const Tuple *t) {
  if (t->type == TUPLE_INT) {
    if (t->length == 1) return t->value->int8;
    if (t->length == 2) return t->value->int16;
    return t->value->int32;
  }
  if (t->type == TUPLE_UINT) {
    if (t->length == 1) return t->value->uint8;
    if (t->length == 2) return t->value->uint16;
    return (int32_t)t->value->uint32;
  }
  return 0;
}

// Die Namen aneinanderreihen, mit Zeilenumbruch dazwischen.
static void prv_namen(char *aus, size_t platz) {
  size_t len = 0;
  aus[0] = '\0';
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    if (i > 0 && len + 1 < platz) {
      aus[len++] = '\n';
      aus[len] = '\0';
    }
    const PlanItem *it = plan_item(i);
    if (!it || !it->used) continue;
    // name[] ist ein festes Feld und muss nicht abgeschlossen sein - erst
    // eine eigene Abschrift macht daraus eine Zeichenkette.
    char name[SC_NAME_LEN + 1];
    memcpy(name, it->name, SC_NAME_LEN);
    name[SC_NAME_LEN] = '\0';
    const size_t n = strlen(name);
    if (len + n < platz) {
      memcpy(aus + len, name, n);
      len += n;
      aus[len] = '\0';
    }
  }
}

/**
 * Die Einstellungen der Uhr - Plan und Animation - in eine Nachricht.
 *
 * DIE UHR IST DIE EINE STELLE, AN DER SIE GELTEN. Geaendert werden sie auf
 * der Konfigseite der Pebble-App (bis zu seiner Archivierung am 29.09.2026
 * auch in Kiesel-Helper); sie schickt an die Uhr, und hier steht, was gilt.
 * Die Telefonseite uebernimmt es in die Konfigseite. Boulder liest den Plan
 * nur mit (fuer die Akte) und aendert ihn nicht.
 */
// Rueckgabe: die Fehlerbits von dict_write (DICT_OK = 0).
static int prv_write_settings(DictionaryIterator *out) {
  uint8_t bytes[SC_MAX_ITEMS * SC_ITEM_BYTES];
  const uint16_t n = plan_to_bytes(bytes);
  int fehler = dict_write_data(out, MESSAGE_KEY_PLAN, bytes, n);
  fehler |= dict_write_int32(out, MESSAGE_KEY_FX, prefs_fx() ? 1 : 0);
  return fehler;
}

// Heute als Kalenderdatum JJJJMMTT - so, wie die Tagesmeldung es nennt.
static int32_t prv_ymd(void) {
  const time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  return (lt->tm_year + 1900) * 10000 + (lt->tm_mon + 1) * 100 + lt->tm_mday;
}

// Boulder nimmt Haken zurueck, deren Einnahme dort geloescht wurde. NUR FUER
// HEUTE: ein Befehl, der ueber Mitternacht liegen blieb, gilt einem Haken,
// den es nicht mehr gibt. Rueckgabe: true, wenn es ein solcher Befehl war.
static bool prv_untake(DictionaryIterator *iter) {
  Tuple *untake = dict_find(iter, MESSAGE_KEY_UNTAKE);
  if (!untake) return false;
  Tuple *tag = dict_find(iter, MESSAGE_KEY_TODAY);
  Tuple *at = dict_find(iter, MESSAGE_KEY_TAKEN_AT);
  if (tag && prv_tuple_int(tag) == prv_ymd() && at && at->type == TUPLE_BYTE_ARRAY &&
      plan_untake((uint32_t)prv_tuple_int(untake), at->value->data, at->length) && s_observer) {
    s_observer();
  }
  return true;
}

static void prv_inbox(DictionaryIterator *iter, void *context) {
  // Die Antwort auf die Frage nach der Zeit (phone_send_today). Sie kommt
  // allein und loest keine Meldung aus - sonst fragte die Uhr endlos.
  Tuple *uhr = dict_find(iter, MESSAGE_KEY_UHRZEIT);
  if (uhr) plan_uhr_bestaetigt((uint32_t)prv_tuple_int(uhr));

  // ZUERST die Einstellungen: sie kommen mit derselben Nachricht wie der
  // Plan, und ein Rueckspringen weiter unten wuerde sie verschlucken.
  Tuple *fx = dict_find(iter, MESSAGE_KEY_FX);
  if (fx) prefs_set_fx(prv_tuple_int(fx) != 0);

  // Auf einen Befehl folgt IMMER die Tagesmeldung, auch wenn kein Haken
  // passte: erst sie sagt dem Telefon, was jetzt gilt.
  const bool befehl = prv_untake(iter);

  Tuple *plan = dict_find(iter, MESSAGE_KEY_PLAN);
  if (!plan || plan->type != TUPLE_BYTE_ARRAY) {
    if (befehl) phone_send_today();
    return;
  }
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
  // Jede Schreibstelle meldet, ob das Feld hineinpasste. Der Postausgang ist
  // fuer den groessten Fall bemessen (siehe OUTBOX_SIZE); passt trotzdem
  // etwas nicht, fehlt es still in der Nachricht - darum steht es im Log.
  // Ob die Anfrage drin war, liest prv_outbox_sent aus der Nachricht selbst.
  int fehler = dict_write_int32(out, MESSAGE_KEY_TODAY, prv_ymd());
  fehler |= dict_write_int32(out, MESSAGE_KEY_DUE, (int32_t)due);
  fehler |= dict_write_int32(out, MESSAGE_KEY_TAKEN, (int32_t)taken);
  // Wann abgehakt wurde: offline Abgehaktes kaeme sonst mit der Zeit des
  // Wiederverbindens in die Akte.
  uint8_t zeiten[SC_TAKEN_AT_BYTES];
  fehler |= dict_write_data(out, MESSAGE_KEY_TAKEN_AT, zeiten, plan_taken_at_to_bytes(zeiten));
  // Die Namen dazu. Das Telefon kennt sie zwar aus der Konfigseite, aber
  // NICHT jede App auf dem Telefon: Boulder (frueher Kiesel-Helper) hoert
  // dieselbe Meldung mit und haette sonst nur Bitmasken ohne Beschriftung.
  char namen[NAMEN_BYTES];
  prv_namen(namen, sizeof(namen));
  fehler |= dict_write_cstring(out, MESSAGE_KEY_NAMES, namen);
  fehler |= prv_write_settings(out);
  if (s_anfrage) {
    fehler |= dict_write_int32(out, MESSAGE_KEY_REQUEST, 1);
    // Sprache der Uhr mitschicken: die Konfigseite wird auf dem TELEFON gebaut
    // und kann sie nicht von sich aus erfahren.
    fehler |= dict_write_int32(out, MESSAGE_KEY_LANG, (int32_t)strings_language());
  }
  // DIE FRAGE NACH DER ZEIT. Steht die Uhr hinter dem gemerkten Tag, weiss
  // nur das Telefon, ob sie jetzt falsch geht (Neustart) oder vorher falsch
  // ging (Audit M10) - siehe plan.c. Es antwortet mit seiner Zeit (prv_inbox).
  // Nur in diesem Zustand: sonst kostet die Frage eine Nachricht je Start.
  if (plan_uhr_fraglich()) {
    fehler |= dict_write_int32(out, MESSAGE_KEY_UHRZEIT, (int32_t)time(NULL));
  }
  if (fehler != DICT_OK) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Tagesmeldung unvollstaendig: %d", fehler);
  }
  const AppMessageResult gesendet = app_message_outbox_send();
  if (gesendet != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Tagesmeldung nicht abgeschickt: %d", (int)gesendet);
    s_unsent = true;
  }
}

// OHNE VERBINDUNG ABGEHAKT: die Meldung scheitert still, und der Pin in der
// Timeline stand weiter als offen da - als haette die App das Abhaken
// vergessen. Sie geht deshalb noch einmal, sobald das Telefon wieder da ist.
static void prv_outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "Meldung nicht angekommen: %d", (int)reason);
  if (dict_find(iter, MESSAGE_KEY_TODAY)) s_unsent = true;
  // Die Startanfrage bleibt stehen (s_anfrage) und faehrt beim Nachholen mit.
  // Ein paar Mal kurz nachfassen: meist laeuft pkjs nur noch an.
  if (s_anfrage && !s_retry && s_nachgefasst < ANFRAGE_NACHFASSEN_MAX) {
    s_nachgefasst++;
    s_retry = app_timer_register(ANFRAGE_NACHFASSEN_MS, prv_retry_cb, NULL);
  }
}

static void prv_outbox_sent(DictionaryIterator *iter, void *context) {
  if (dict_find(iter, MESSAGE_KEY_TODAY)) s_unsent = false;
  if (dict_find(iter, MESSAGE_KEY_REQUEST)) s_anfrage = false;
}

static void prv_connection(bool connected) {
  if (connected && s_unsent) phone_send_today();
}

static void prv_ready(void *data) {
  // Einmal anfragen. Laeuft kein Telefon, bleibt es beim gespeicherten Plan -
  // die Uhr ist darauf nicht angewiesen.
  // DIE ANFRAGE TRAEGT DEN PLAN DER UHR. Ohne ihn hielt die Telefonseite die
  // Uhr fuer eine alte Fassung und schickte ihren eigenen gespeicherten Plan
  // zurueck - ueber den hinweg, den die Uhr schon hatte (etwa aus dem
  // archivierten Kiesel-Helper; Boulder liest den Plan nur). Und eine
  // frisch installierte Uhr ohne Plan war so nicht zu erkennen. Der Tagesstand
  // reist mit, damit die Pins beim Start auch dann nachziehen, wenn das
  // Telefon den Plan nur uebernimmt und nichts zurueckschickt.
  s_anfrage = true;
  phone_send_today();
}

void phone_init(void) {
  app_message_register_inbox_received(prv_inbox);
  app_message_register_outbox_failed(prv_outbox_failed);
  app_message_register_outbox_sent(prv_outbox_sent);
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = prv_connection,
  });
  app_message_open(INBOX_SIZE, OUTBOX_SIZE);
  // Nicht sofort: pkjs braucht einen Moment, bis es zuhoert.
  app_timer_register(1500, prv_ready, NULL);
}

void phone_set_observer(void (*on_plan)(void)) {
  s_observer = on_plan;
}
