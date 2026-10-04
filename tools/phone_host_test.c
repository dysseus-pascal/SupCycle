// Der Weg zum Telefon (phone.c) - auf dem Rechner, mit einer Attrappe fuer
// AppMessage, Zeitgeber und Verbindung (tools/host/attrappe.c).
//
//   sh tools/phone_host_test.sh
//
// Was hier leicht falsch und teuer ist:
//
//   - Die STARTANFRAGE (REQUEST) ist erst erledigt, wenn das Telefon sie
//     bestaetigt. Bis 0.14.0 galt sie schon beim Abschicken als erledigt: hoerte
//     pkjs noch nicht zu (NACK), ging beim Nachholen nur der - bei einer
//     frischen Uhr leere - Plan ohne REQUEST hinaus, und das Telefon legte ihn
//     ueber seinen gespeicherten. Im Emulator nachgestellt (03.10.2026).
//   - Ein NACK beim Start wird ein paar Mal nachgefasst, aber nicht endlos.
//   - Was nicht in den Postausgang passt oder gar nicht hinausgeht, darf nicht
//     still fehlen: es steht im Log, und die Meldung geht beim naechsten
//     Verbinden noch einmal.
//   - DIE FRAGE NACH DER ZEIT (Audit M10): steht die Uhr hinter ihrem
//     gemerkten Tag, traegt jede Meldung UHRZEIT; die Antwort des Telefons
//     bestaetigt die Uhr oder nicht - und loest selbst keine Meldung aus.
//
// Exitcode 0 = alles wie zugesagt.
#include <pebble.h>
#include <sys/wait.h>
#include <unistd.h>
#include "phone.h"
#include "plan.h"
#include "prefs.h"
#include "strings.h"

time_t stub_jetzt;

static int s_fehler;
static void pruefe(const char *was, bool ok) {
  printf("%s %s\n", ok ? "  ok    " : "  FEHLER", was);
  if (!ok) s_fehler++;
}

static bool hat(uint32_t key) {
  return attrappe_letzte() && dict_find(attrappe_letzte(), key) != NULL;
}
static int32_t zahl(uint32_t key) {
  Tuple *t = attrappe_letzte() ? dict_find(attrappe_letzte(), key) : NULL;
  return t ? t->value->int32 : -999;
}
static bool im_log(const char *text) { return strstr(attrappe_log_text, text) != NULL; }

// 03.10.2026, 08:00 UTC - der Test laeuft mit TZ=UTC.
#define MORGEN 1791014400
// Tagesnummer des 03.10.2026 und das Persist-Fach des gemerkten Tages
// (plan.c), mit Wert festgenagelt.
#define TAG_0310 20729
#define FACH_TAG 2

// App frisch starten: leerer Persist (frische Uhr), Sprache Deutsch.
static void start(void) {
  attrappe_persist_leeren();
  attrappe_nachrichten_leeren();
  attrappe_log_leeren();
  while (attrappe_zeitgeber_offen()) attrappe_zeitgeber_ablaufen();
  attrappe_nachrichten_leeren();
  stub_jetzt = MORGEN;
  attrappe_sprache = "de_DE";
  strings_refresh();
  prefs_init();
  plan_init();
  phone_init();
}

// Ein voller Plan: sechs Plaetze mit 15 Byte langen Namen, alle abgehakt.
static void voller_plan(void) {
  uint8_t b[SC_MAX_ITEMS * SC_ITEM_BYTES];
  memset(b, 0, sizeof(b));
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    uint8_t *p = b + i * SC_ITEM_BYTES;
    memcpy(p, "Magnesiumcitrat", 15);
    p[15] = 0;
    p[16] = 8; p[18] = 1; p[19] = 1;
    p[22] = 0x19; p[23] = 0x51;      // Ankertag 20761
  }
  plan_set_from_bytes(b, sizeof(b));
  for (int i = 0; i < SC_MAX_ITEMS; i++) plan_set_taken(i, true);
}

static void abschnitt_0(void) {
  printf("\nStart\n");
  start();
  pruefe("Postausgang 512 Byte", attrappe_ausgang_groesse() == 512);
  pruefe("Anfrage erst nach 1500 ms (pkjs braucht einen Moment)",
         attrappe_zeitgeber_offen() == 1 && attrappe_zeitgeber_ms(0) == 1500);
  attrappe_zeitgeber_ablaufen();
  pruefe("Startmeldung traegt REQUEST", attrappe_gesendet() == 1 && zahl(MESSAGE_KEY_REQUEST) == 1);
  pruefe("Startmeldung traegt die Sprache (Deutsch = 1)", zahl(MESSAGE_KEY_LANG) == 1);
  pruefe("Startmeldung traegt den Plan (156 Byte)",
         hat(MESSAGE_KEY_PLAN) && dict_find(attrappe_letzte(), MESSAGE_KEY_PLAN)->length == 156);
  pruefe("Startmeldung traegt den Tag JJJJMMTT", zahl(MESSAGE_KEY_TODAY) == 20261003);
}

static void abschnitt_1(void) {
  printf("\nStartanfrage abgelehnt (H3)\n");
  start();
  attrappe_zeitgeber_ablaufen();
  attrappe_nack(APP_MSG_SEND_REJECTED);
  pruefe("Ablehnung steht im Log", im_log("Meldung nicht angekommen: 4"));
  pruefe("nachgefasst wird nach 3000 ms",
         attrappe_zeitgeber_offen() == 1 && attrappe_zeitgeber_ms(0) == 3000);
  attrappe_zeitgeber_ablaufen();
  pruefe("das Nachfassen traegt REQUEST", attrappe_gesendet() == 2 && zahl(MESSAGE_KEY_REQUEST) == 1);
  pruefe("und die Sprache", zahl(MESSAGE_KEY_LANG) == 1);
  // Drei weitere Ablehnungen: insgesamt vier Mal nachgefasst, dann Ruhe.
  for (int i = 0; i < 3; i++) {
    attrappe_nack(APP_MSG_SEND_REJECTED);
    attrappe_zeitgeber_ablaufen();
  }
  pruefe("vier Mal nachgefasst", attrappe_gesendet() == 5);
  attrappe_nack(APP_MSG_SEND_TIMEOUT);
  pruefe("nach dem vierten nicht mehr", attrappe_zeitgeber_offen() == 0);
  attrappe_verbindung(true);
  pruefe("beim Wiederverbinden geht sie noch einmal - MIT REQUEST",
         attrappe_gesendet() == 6 && zahl(MESSAGE_KEY_REQUEST) == 1);
  attrappe_ack();
  phone_send_today();
  pruefe("erst nach der Bestaetigung ohne REQUEST", attrappe_gesendet() == 7 && !hat(MESSAGE_KEY_REQUEST));
  pruefe("und ohne Sprache", !hat(MESSAGE_KEY_LANG));
}

static void abschnitt_2(void) {
  printf("\nStartanfrage gleich bestaetigt\n");
  start();
  attrappe_zeitgeber_ablaufen();
  attrappe_ack();
  pruefe("kein Nachfassen", attrappe_zeitgeber_offen() == 0);
  phone_send_today();
  pruefe("Tagesmeldung danach ohne REQUEST", !hat(MESSAGE_KEY_REQUEST));
  attrappe_nack(APP_MSG_NOT_CONNECTED);
  pruefe("abgelehnte Tagesmeldung: kein Nachfassen der Anfrage", attrappe_zeitgeber_offen() == 0);
  attrappe_verbindung(true);
  pruefe("abgelehnte Tagesmeldung geht beim Wiederverbinden", attrappe_gesendet() == 3 && !hat(MESSAGE_KEY_REQUEST));
  attrappe_ack();
  attrappe_verbindung(true);
  pruefe("bestaetigt: beim naechsten Verbinden nichts", attrappe_gesendet() == 3);
}

static void abschnitt_3(void) {
  printf("\nBesetzt\n");
  start();
  attrappe_zeitgeber_ablaufen();
  phone_send_today();               // die Startmeldung ist noch unterwegs
  pruefe("besetzt: nach 700 ms noch einmal", attrappe_zeitgeber_offen() == 1 && attrappe_zeitgeber_ms(0) == 700);
  attrappe_nack(APP_MSG_SEND_REJECTED);
  attrappe_zeitgeber_ablaufen();
  pruefe("das zweite Mal traegt die noch offene Anfrage", zahl(MESSAGE_KEY_REQUEST) == 1);
}

static void abschnitt_4(void) {
  printf("\nGroesster Fall passt\n");
  start();
  voller_plan();
  attrappe_zeitgeber_ablaufen();
  pruefe("voller Plan mit Anfrage: nichts fehlt", !im_log("unvollstaendig"));
  pruefe("alle neun Felder sind da", hat(MESSAGE_KEY_TODAY) && hat(MESSAGE_KEY_DUE) && hat(MESSAGE_KEY_TAKEN) &&
         hat(MESSAGE_KEY_TAKEN_AT) && hat(MESSAGE_KEY_NAMES) && hat(MESSAGE_KEY_PLAN) && hat(MESSAGE_KEY_FX) &&
         hat(MESSAGE_KEY_REQUEST) && hat(MESSAGE_KEY_LANG));
  pruefe("alle sechs abgehakt", zahl(MESSAGE_KEY_TAKEN) == 0x3f);
  printf("         (Nachricht %u von %u Byte)\n", (unsigned)attrappe_letzte()->belegt,
         (unsigned)attrappe_ausgang_groesse());
}

static void abschnitt_6(void) {
  printf("\nPasst nicht: steht im Log\n");
  start();
  voller_plan();
  attrappe_ausgang_begrenzen(200);   // zu klein fuer Plan und Namen
  attrappe_zeitgeber_ablaufen();
  pruefe("unvollstaendige Meldung steht im Log", im_log("Tagesmeldung unvollstaendig: 2"));
  pruefe("abgeschickt wird sie trotzdem", attrappe_gesendet() == 1);
}

static void abschnitt_nicht_abgeschickt(void) {
  printf("\nNicht abgeschickt: steht im Log, geht beim Verbinden noch einmal\n");
  start();
  attrappe_zeitgeber_ablaufen();
  attrappe_ack();                    // die Startanfrage ist erledigt
  attrappe_log_leeren();
  attrappe_senden_scheitert(APP_MSG_NOT_CONNECTED);
  phone_send_today();
  pruefe("steht im Log", im_log("Tagesmeldung nicht abgeschickt: 8"));
  pruefe("nichts ging hinaus", attrappe_gesendet() == 1 && !attrappe_unterwegs());
  attrappe_verbindung(true);
  pruefe("beim Verbinden geht sie noch einmal", attrappe_gesendet() == 2 && hat(MESSAGE_KEY_TODAY));
  attrappe_ack();
  attrappe_verbindung(true);
  pruefe("bestaetigt: beim naechsten Verbinden nichts mehr", attrappe_gesendet() == 2);
}

// Start mit einem gemerkten Tag (Tagesnummer) - etwa einem, an dem die Uhr
// vorging.
static void start_gemerkt(int32_t tag) {
  attrappe_persist_leeren();
  attrappe_nachrichten_leeren();
  attrappe_log_leeren();
  while (attrappe_zeitgeber_offen()) attrappe_zeitgeber_ablaufen();
  attrappe_nachrichten_leeren();
  stub_jetzt = MORGEN;
  attrappe_sprache = "de_DE";
  strings_refresh();
  persist_write_int(FACH_TAG, tag);
  prefs_init();
  plan_init();
  phone_init();
}

static void zeit_vom_telefon(uint32_t t) {
  DictionaryIterator *ein = attrappe_eingang_beginn();
  dict_write_int32(ein, MESSAGE_KEY_UHRZEIT, (int32_t)t);
  attrappe_eingang_zustellen();
}

static void abschnitt_uhrzeit(void) {
  printf("\nFrage nach der Zeit (M10)\n");
  start();
  attrappe_zeitgeber_ablaufen();
  pruefe("Uhr auf dem gemerkten Tag: keine Frage", !hat(MESSAGE_KEY_UHRZEIT));

  start_gemerkt(TAG_0310 + 1);       // die Uhr ging einen Tag vor
  attrappe_zeitgeber_ablaufen();
  pruefe("Uhr hinter dem gemerkten Tag: die Startmeldung fragt, mit der Zeit der Uhr",
         zahl(MESSAGE_KEY_UHRZEIT) == MORGEN && zahl(MESSAGE_KEY_REQUEST) == 1);
  attrappe_ack();
  const int vorher = attrappe_gesendet();
  zeit_vom_telefon(MORGEN + 400);    // das Telefon steht 400 s weiter: Uhr falsch
  pruefe("400 s Abweichung: der gemerkte Tag bleibt", persist_read_int(FACH_TAG) == TAG_0310 + 1);
  pruefe("400 s Abweichung: steht im Log", im_log("Telefonzeit weicht -400 s ab"));
  pruefe("die Antwort loest keine Meldung aus", attrappe_gesendet() == vorher);
  phone_send_today();
  pruefe("die naechste Meldung fragt wieder", hat(MESSAGE_KEY_UHRZEIT));
  attrappe_ack();
  zeit_vom_telefon(MORGEN + 20);     // passt: die Uhr geht richtig
  pruefe("20 s Abweichung: heute gilt", persist_read_int(FACH_TAG) == TAG_0310);
  pruefe("bestaetigt: steht im Log", im_log("Uhr vom Telefon bestaetigt"));
  pruefe("auch diese Antwort loest keine Meldung aus", attrappe_gesendet() == vorher + 1);
  phone_send_today();
  pruefe("danach fragt die Uhr nicht mehr", attrappe_gesendet() == vorher + 2 && !hat(MESSAGE_KEY_UHRZEIT));
}

static void abschnitt_uhrzeit_groesster_fall(void) {
  printf("\nGroesster Fall mit der Frage nach der Zeit\n");
  start_gemerkt(TAG_0310 + 1);
  voller_plan();
  attrappe_zeitgeber_ablaufen();
  pruefe("voller Plan, Anfrage und Frage: nichts fehlt", !im_log("unvollstaendig"));
  pruefe("alle zehn Felder sind da", hat(MESSAGE_KEY_TODAY) && hat(MESSAGE_KEY_DUE) && hat(MESSAGE_KEY_TAKEN) &&
         hat(MESSAGE_KEY_TAKEN_AT) && hat(MESSAGE_KEY_NAMES) && hat(MESSAGE_KEY_PLAN) && hat(MESSAGE_KEY_FX) &&
         hat(MESSAGE_KEY_REQUEST) && hat(MESSAGE_KEY_LANG) && hat(MESSAGE_KEY_UHRZEIT));
  printf("         (Nachricht %u von %u Byte)\n", (unsigned)attrappe_letzte()->belegt,
         (unsigned)attrappe_ausgang_groesse());
  // Von Hand gezaehlt: 1 + TODAY, DUE, TAKEN, FX, REQUEST, LANG, UHRZEIT je
  // 7 + 4 (77) + TAKEN_AT 7 + 24 (31) + NAMES 7 + 6 x 15 + 5 + 1 (103)
  // + PLAN 7 + 156 (163) = 375.
  pruefe("groesster Fall: 375 Byte", attrappe_letzte()->belegt == 375);
}

static void abschnitt_5(void) {
  printf("\nPlan vom Telefon\n");
  start();
  attrappe_zeitgeber_ablaufen();
  attrappe_ack();
  DictionaryIterator *ein = attrappe_eingang_beginn();
  uint8_t b[SC_MAX_ITEMS * SC_ITEM_BYTES];
  memset(b, 0, sizeof(b));
  memcpy(b, "Zink", 4);
  b[16] = 21; b[17] = 15; b[18] = 1; b[19] = 1;
  dict_write_data(ein, MESSAGE_KEY_PLAN, b, sizeof(b));
  dict_write_int32(ein, MESSAGE_KEY_FX, 0);
  attrappe_eingang_zustellen();
  pruefe("Plan uebernommen", plan_count() == 1 && strcmp(plan_item(0)->name, "Zink") == 0);
  pruefe("Animation aus", !prefs_fx());
  pruefe("Antwort mit dem neuen Plan, ohne REQUEST",
         attrappe_gesendet() == 2 && hat(MESSAGE_KEY_PLAN) && !hat(MESSAGE_KEY_REQUEST) &&
         zahl(MESSAGE_KEY_FX) == 0);
}

// Jeder Abschnitt laeuft in einem eigenen Prozess: so hat er frische
// statische Variablen wie die App bei jedem Start auf der Uhr.
static void (*const ABSCHNITTE[])(void) = {
  abschnitt_0, abschnitt_1, abschnitt_2, abschnitt_3, abschnitt_4, abschnitt_5, abschnitt_6,
  abschnitt_nicht_abgeschickt, abschnitt_uhrzeit, abschnitt_uhrzeit_groesster_fall,
};

int main(void) {
  int fehler = 0;
  for (unsigned i = 0; i < sizeof(ABSCHNITTE) / sizeof(ABSCHNITTE[0]); i++) {
    fflush(stdout);
    const pid_t kind = fork();
    if (kind == 0) {
      ABSCHNITTE[i]();
      fflush(stdout);
      _exit(s_fehler > 100 ? 100 : s_fehler);
    }
    int status = 0;
    waitpid(kind, &status, 0);
    fehler += WIFEXITED(status) ? WEXITSTATUS(status) : 1;
  }
  printf("%s\n", fehler ? "NICHT BESTANDEN" : "alles bestanden");
  return fehler ? 1 : 0;
}
