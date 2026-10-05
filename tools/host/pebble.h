// Nur fuer die Host-Tests in tools/: so viel vom Pebble-SDK, wie die App
// braucht. Persist liegt im Speicher (attrappe_persist.c), die Uhrzeit
// stellt der Test, Nachrichten, Zeitgeber, Verbindung und Wecker spielt die
// Attrappe (attrappe.c). Fenster, Ebenen und Zeichnen (attrappe_ui.c) braucht
// nur der Test der ganzen App (app_host_test.c).
//
// WARUM NICHT IM EMULATOR: dort laesst sich die Zeit nicht halten (jeder
// pebble-Befehl stellt sie neu), und ein Neustart der App mitten im Tag ist
// muehsam nachzustellen. Hier ist beides eine Zeile.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define APP_LOG_LEVEL_INFO 0
#define APP_LOG_LEVEL_WARNING 1
#define APP_LOG_LEVEL_ERROR 2
// Das Log landet in attrappe_log_text: ein Test kann pruefen, dass ein
// Fehler NICHT still bleibt.
void attrappe_log(int level, const char *fmt, ...);
extern char attrappe_log_text[8192];
void attrappe_log_leeren(void);
#define APP_LOG(level, fmt, ...) attrappe_log((level), (fmt), ##__VA_ARGS__)

// Die Uhrzeit der Uhr - der Test setzt sie.
extern time_t stub_jetzt;
static inline time_t stub_time(time_t *t) {
  if (t) *t = stub_jetzt;
  return stub_jetzt;
}
#define time(t) stub_time(t)

bool persist_exists(uint32_t key);
int persist_read_data(uint32_t key, void *buf, size_t size);
int32_t persist_read_int(uint32_t key);
int persist_write_data(uint32_t key, const void *data, size_t size);
int persist_write_int(uint32_t key, int32_t value);
int persist_delete(uint32_t key);
int persist_get_size(uint32_t key);   //< Byte im Fach, sonst E_DOES_NOT_EXIST
#define E_DOES_NOT_EXIST (-9)    // wie im SDK
void attrappe_persist_leeren(void);

// --- Dictionary im Format der Pebble: 1 Byte Anzahl, je Tupel 4 Byte
// Schluessel, 1 Byte Typ, 2 Byte Laenge, dann die Daten. So zaehlt auch der
// Platz wie auf der Uhr - ein Feld, das dort nicht passt, passt hier nicht.
typedef enum { TUPLE_BYTE_ARRAY = 0, TUPLE_CSTRING = 1, TUPLE_UINT = 2, TUPLE_INT = 3 } TupleType;
typedef struct __attribute__((__packed__)) {
  uint32_t key;
  uint8_t type;
  uint16_t length;
  union {
    uint8_t data[0];
    char cstring[0];
    uint8_t uint8;
    uint16_t uint16;
    uint32_t uint32;
    int8_t int8;
    int16_t int16;
    int32_t int32;
  } value[];
} Tuple;
typedef struct {
  uint8_t *puffer;
  uint16_t groesse;
  uint16_t belegt;
} DictionaryIterator;
typedef enum {
  DICT_OK = 0,
  DICT_NOT_ENOUGH_STORAGE = 1 << 1,
  DICT_INVALID_ARGS = 1 << 2,
} DictionaryResult;
DictionaryResult dict_write_begin(DictionaryIterator *iter, uint8_t *buffer, uint16_t size);
DictionaryResult dict_write_data(DictionaryIterator *iter, uint32_t key, const uint8_t *data, uint16_t size);
DictionaryResult dict_write_cstring(DictionaryIterator *iter, uint32_t key, const char *cstring);
DictionaryResult dict_write_int32(DictionaryIterator *iter, uint32_t key, int32_t value);
uint32_t dict_write_end(DictionaryIterator *iter);
Tuple *dict_find(const DictionaryIterator *iter, uint32_t key);

// --- AppMessage ---
typedef enum {
  APP_MSG_OK = 0,
  APP_MSG_SEND_TIMEOUT = 1 << 1,
  APP_MSG_SEND_REJECTED = 1 << 2,
  APP_MSG_NOT_CONNECTED = 1 << 3,
  APP_MSG_BUSY = 1 << 6,
  APP_MSG_INVALID_STATE = 1 << 15,
} AppMessageResult;
typedef void (*AppMessageInboxReceived)(DictionaryIterator *iterator, void *context);
typedef void (*AppMessageOutboxSent)(DictionaryIterator *iterator, void *context);
typedef void (*AppMessageOutboxFailed)(DictionaryIterator *iterator, AppMessageResult reason, void *context);
AppMessageInboxReceived app_message_register_inbox_received(AppMessageInboxReceived cb);
AppMessageOutboxSent app_message_register_outbox_sent(AppMessageOutboxSent cb);
AppMessageOutboxFailed app_message_register_outbox_failed(AppMessageOutboxFailed cb);
AppMessageResult app_message_open(uint32_t size_inbound, uint32_t size_outbound);
AppMessageResult app_message_outbox_begin(DictionaryIterator **iterator);
AppMessageResult app_message_outbox_send(void);
// Was der Test damit tut: das Telefon antworten lassen und nachsehen.
void attrappe_nachrichten_leeren(void);
uint32_t attrappe_ausgang_groesse(void);       //< was app_message_open verlangte
void attrappe_ausgang_begrenzen(uint16_t groesse); //< kleinerer Postausgang (Fehlerfall)
void attrappe_senden_scheitert(AppMessageResult grund); //< naechstes outbox_send liefert das
int attrappe_gesendet(void);                   //< wie viele Nachrichten hinausgingen
DictionaryIterator *attrappe_letzte(void);     //< Abschrift der zuletzt gesendeten
bool attrappe_unterwegs(void);                 //< wartet eine auf ACK/NACK?
void attrappe_ack(void);
void attrappe_nack(AppMessageResult grund);
// Eine Nachricht des Telefons an die Uhr: beginnen, mit dict_write fuellen,
// zustellen.
DictionaryIterator *attrappe_eingang_beginn(void);
void attrappe_eingang_zustellen(void);

// --- Zeitgeber ---
typedef struct AppTimer AppTimer;
typedef void (*AppTimerCallback)(void *data);
AppTimer *app_timer_register(uint32_t timeout_ms, AppTimerCallback callback, void *callback_data);
void app_timer_cancel(AppTimer *timer);
int attrappe_zeitgeber_offen(void);
uint32_t attrappe_zeitgeber_ms(int nummer);    //< Dauer des n-ten offenen
int attrappe_zeitgeber_ablaufen(void);         //< alle offenen einmal ausloesen
void attrappe_zeitgeber_vorspulen(uint32_t ms); //< so viel Zeit vergeht, der Reihe nach

// --- Verbindung ---
typedef void (*ConnectionHandler)(bool connected);
typedef struct {
  ConnectionHandler pebble_app_connection_handler;
  ConnectionHandler pebblekit_connection_handler;
} ConnectionHandlers;
void connection_service_subscribe(ConnectionHandlers handlers);
void attrappe_verbindung(bool da);

const char *i18n_get_system_locale(void);
extern const char *attrappe_sprache;

// --- Wecker, wie pebbleos services/wakeup/service.c ---
// Hoechstens 8 je App (E_OUT_OF_RESOURCES), keiner in der Minute um einen
// anderen (E_RANGE), keiner in der Vergangenheit (E_INVALID_ARGUMENT).
// Feuert einer, ist er weg; laeuft die App, bekommt ihn der Abonnent.
#define E_INVALID_ARGUMENT (-4)
#define E_OUT_OF_RESOURCES (-7)
#define E_RANGE (-8)
typedef int32_t WakeupId;
typedef void (*WakeupHandler)(WakeupId wakeup_id, int32_t cookie);
typedef enum {
  APP_LAUNCH_SYSTEM = 0, APP_LAUNCH_USER, APP_LAUNCH_PHONE, APP_LAUNCH_WAKEUP,
  APP_LAUNCH_WORKER, APP_LAUNCH_QUICK_LAUNCH, APP_LAUNCH_TIMELINE_ACTION, APP_LAUNCH_SMARTSTRAP,
} AppLaunchReason;
WakeupId wakeup_schedule(time_t timestamp, int32_t cookie, bool notify_if_missed);
void wakeup_cancel_all(void);
bool wakeup_get_launch_event(WakeupId *wakeup_id, int32_t *cookie);
void wakeup_service_subscribe(WakeupHandler handler);
AppLaunchReason launch_reason(void);
typedef struct { WakeupId id; time_t zeit; int32_t cookie; bool melden; } AttrappeWecker;
void attrappe_wecker_leeren(void);
int attrappe_wecker_zahl(void);
const AttrappeWecker *attrappe_wecker(int nummer);   //< nach Zeit sortiert
// Wie die App gestartet wird: von Hand oder durch einen Wecker mit Cookie.
void attrappe_start(AppLaunchReason grund, int32_t cookie);
// Der frueheste Wecker feuert, waehrend die App laeuft: Uhr auf seine Zeit,
// Eintrag weg, Abonnent gerufen. false, wenn keiner steht.
bool attrappe_wecker_feuert(void);
bool attrappe_wecker_abonniert(void);
// Die Uhr ist aus bis `bis` und startet dann neu (pebbleos wakeup_init):
// abgelaufene Wecker sind weg, die mit melden zaehlen als verpasst.
// Rueckgabe: wie viele verpasste sich melden.
int attrappe_uhr_aus_bis(time_t bis);

// --- Was die ganze App zusaetzlich braucht (attrappe_ui.c) ---
#include "pebble_ui.h"
