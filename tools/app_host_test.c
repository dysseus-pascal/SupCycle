// Die ganze App auf dem Rechner: supcycle.c mit allen Fenstern, gestartet
// wie auf der Uhr - von Hand oder durch einen Wecker. Fenster, Tasten und
// Wecker kommen aus den Attrappen in tools/host (pebble_ui.h, attrappe.c).
//
//   sh tools/app_host_test.sh
//
// Was hier leicht falsch und teuer ist:
//
//   - DER WECKER ZUM NEUPLANEN (W-K1) darf nachts nichts zeigen und nichts
//     senden: Wecker stellen, App zu.
//   - EIN WECKER BEI OFFENER APP (W-H2) erinnert genauso wie einer, der die
//     App startet - bis 0.15.0 verpuffte er.
//   - NACH "GENOMMEN" (M3) geht die App erst zu, wenn das Telefon den Haken
//     hat, hoechstens 5 s spaeter - ohne Verbindung sofort.
//
// Exitcode 0 = alles wie zugesagt.
#define _DEFAULT_SOURCE
#include <pebble.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "plan.h"
#include "strings.h"

time_t stub_jetzt;
int supcycle_main(void);   // main() aus src/c/supcycle.c, umbenannt

static int s_fehler;
static void pruefe(const char *was, bool ok) {
  printf("%s %s\n", ok ? "  ok    " : "  FEHLER", was);
  if (!ok) s_fehler++;
}

#define COOKIE_NEUPLANEN 5000
#define FACH_PLAN 1
#define FACH_FX 4
#define FACH_TAGE 9

static time_t ortszeit(int j, int mo, int t, int h, int mi) {
  struct tm tm = { .tm_year = j - 1900, .tm_mon = mo - 1, .tm_mday = t,
                   .tm_hour = h, .tm_min = mi, .tm_isdst = -1 };
  return mktime(&tm);
}
static int32_t datum_tag(int j, int mo, int t) {
  struct tm tm = { .tm_year = j - 1900, .tm_mon = mo - 1, .tm_mday = t };
  return (int32_t)(timegm(&tm) / 86400);
}
static bool ist_um(time_t t, int j, int mo, int tag, int h, int mi) {
  struct tm soll = { .tm_year = j - 1900, .tm_mon = mo - 1, .tm_mday = tag, .tm_hour = 12, .tm_isdst = -1 };
  mktime(&soll);
  const struct tm ist = *localtime(&t);
  return ist.tm_year == soll.tm_year && ist.tm_mon == soll.tm_mon && ist.tm_mday == soll.tm_mday &&
         ist.tm_hour == h && ist.tm_min == mi;
}
static bool wecker_um(int32_t cookie, int j, int mo, int t, int h, int mi) {
  for (int i = 0; i < attrappe_wecker_zahl(); i++) {
    const AttrappeWecker *w = attrappe_wecker(i);
    if (w->cookie == cookie && ist_um(w->zeit, j, mo, t, h, mi)) return true;
  }
  return false;
}

// Die Uhr, wie man sie antrifft: ein Plan im Persist, Animation an oder aus.
// Taeglich: Zink 08:00 und D3 08:00, Maca 12:30.
static void uhr(int32_t heute, bool fx) {
  attrappe_persist_leeren();
  attrappe_nachrichten_leeren();
  attrappe_log_leeren();
  attrappe_wecker_leeren();
  attrappe_ui_leeren();
  attrappe_sprache = "de_DE";
  uint8_t b[SC_MAX_ITEMS * SC_ITEM_BYTES];
  memset(b, 0, sizeof(b));
  const char *namen[] = { "Zink", "D3", "Maca" };
  const int zeiten[][2] = { { 8, 0 }, { 8, 0 }, { 12, 30 } };
  for (int i = 0; i < 3; i++) {
    uint8_t *p = b + i * SC_ITEM_BYTES;
    memcpy(p, namen[i], strlen(namen[i]));
    p[16] = (uint8_t)zeiten[i][0]; p[17] = (uint8_t)zeiten[i][1]; p[18] = 1; p[19] = 1;
    for (int k = 0; k < 4; k++) p[22 + k] = (uint8_t)((uint32_t)heute >> (8 * k));
  }
  persist_write_data(FACH_PLAN, b, sizeof(b));
  persist_write_int(FACH_FX, fx ? 1 : 0);
  persist_write_int(FACH_TAGE, 2);   // schon Kalendertage
}

static int s_lief;

// --- Der Wecker zum Neuplanen ---
static void lief_mit_fenster(void) { s_lief++; }
static void abschnitt_neuplanen(void) {
  printf("\nDer Wecker zum Neuplanen startet die App (W-K1)\n");
  uhr(datum_tag(2026, 7, 14), true);
  stub_jetzt = ortszeit(2026, 7, 14, 3, 0);
  attrappe_start(APP_LAUNCH_WAKEUP, COOKIE_NEUPLANEN);
  s_lief = 0;
  attrappe_app_laeuft = lief_mit_fenster;
  supcycle_main();
  pruefe("kein Fenster: die App ist gleich wieder zu", s_lief == 0 && attrappe_fenster_zahl() == 0);
  pruefe("nichts ans Telefon", attrappe_gesendet() == 0 && attrappe_zeitgeber_offen() == 0);
  pruefe("keine Vibration", attrappe_vibrationen() == 0);
  pruefe("Erinnerung heute 08:00 neu gestellt", wecker_um(480, 2026, 7, 14, 8, 0));
  pruefe("und der naechste Wecker zum Neuplanen morgen 03:00", wecker_um(COOKIE_NEUPLANEN, 2026, 7, 15, 3, 0));
  pruefe("der Starter zeigt den neuen Tag", strstr(attrappe_glance(), "3 von 3") != NULL);
}

// --- Ein Wecker bei offener App ---
static void bei_offener_app(void) {
  s_lief++;
  pruefe("die App hoert auf Wecker", attrappe_wecker_abonniert());
  const int vorher = attrappe_vibrationen();
  attrappe_wecker_feuert();          // der von 08:00
  pruefe("ein Erinnerungsfenster liegt ueber dem Heute-Schirm", attrappe_fenster_zahl() == 2);
  pruefe("und es vibriert", attrappe_vibrationen() > vorher);
  attrappe_zeichnen();
  pruefe("es zeigt die Runde 08:00 mit beiden",
         strstr(attrappe_texte(), "08:00") && strstr(attrappe_texte(), "Zink") && strstr(attrappe_texte(), "D3"));
  pruefe("die Kette ist weiter gestellt: morgen 08:00", wecker_um(480, 2026, 7, 15, 8, 0));
  pruefe("und 12:30 heute bleibt", wecker_um(750, 2026, 7, 14, 12, 30));
  // Die naechste Runde, waehrend die erste noch offen steht: sie verpufft
  // nicht, die Kette geht weiter.
  attrappe_wecker_feuert();          // der von 12:30
  pruefe("auch die naechste Runde stellt die Kette weiter: morgen 12:30", wecker_um(750, 2026, 7, 15, 12, 30));
}
static void abschnitt_offen(void) {
  printf("\nWecker bei offener App (W-H2)\n");
  uhr(datum_tag(2026, 7, 14), true);
  stub_jetzt = ortszeit(2026, 7, 14, 7, 59);
  attrappe_start(APP_LAUNCH_USER, 0);
  s_lief = 0;
  attrappe_app_laeuft = bei_offener_app;
  supcycle_main();
  pruefe("die App lief", s_lief == 1);
}

// --- Nach "Genommen" ---
typedef enum { ACK, KEINE_ANTWORT, OHNE_VERBINDUNG } Antwort;
static Antwort s_antwort;
static bool s_mit_fx;
static void genommen(void) {
  s_lief++;
  // Die Startanfrage ist durch; die Erinnerung steht.
  attrappe_zeitgeber_vorspulen(1500);
  attrappe_ack();
  pruefe("die Erinnerung steht", attrappe_fenster_zahl() == 2);
  if (s_antwort == OHNE_VERBINDUNG) attrappe_senden_scheitert(APP_MSG_NOT_CONNECTED);
  attrappe_taste(BUTTON_ID_SELECT);
  pruefe("abgehakt", plan_taken(0) && plan_taken(1) && !plan_taken(2));
  if (s_mit_fx) {
    pruefe("Pilly spielt, die App bleibt offen", attrappe_fenster_zahl() == 2);
    attrappe_animationen_beenden();
  }
  if (s_antwort == OHNE_VERBINDUNG) {
    pruefe("ohne Verbindung: gleich zu", attrappe_fenster_zahl() == 0);
    return;
  }
  pruefe("die Meldung ist unterwegs: die App bleibt offen", attrappe_fenster_zahl() == 2 && attrappe_unterwegs());
  if (!s_mit_fx) {
    // Die Leiste steht noch: Zurueck hiesse sonst "Runde verfaellt".
    attrappe_taste(BUTTON_ID_BACK);
    pruefe("Zurueck waehrend des Wartens aendert nichts", attrappe_fenster_zahl() == 2 && plan_taken(0));
  }
  if (s_antwort == ACK) {
    attrappe_zeitgeber_vorspulen(1000);
    pruefe("nach 1 s ohne Antwort: noch offen", attrappe_fenster_zahl() == 2);
    attrappe_ack();
    pruefe("bestaetigt: die App geht zu", attrappe_fenster_zahl() == 0);
  } else {
    attrappe_zeitgeber_vorspulen(4900);
    pruefe("nach 4,9 s ohne Antwort: noch offen", attrappe_fenster_zahl() == 2);
    attrappe_zeitgeber_vorspulen(200);
    pruefe("nach 5 s: zu", attrappe_fenster_zahl() == 0);
  }
}
static void abschnitt_genommen(bool fx, Antwort antwort, const char *titel) {
  printf("\n%s\n", titel);
  uhr(datum_tag(2026, 7, 14), fx);
  stub_jetzt = ortszeit(2026, 7, 14, 8, 0);
  attrappe_start(APP_LAUNCH_WAKEUP, 480);
  s_lief = 0;
  s_mit_fx = fx;
  s_antwort = antwort;
  attrappe_app_laeuft = genommen;
  supcycle_main();
  pruefe("die App lief", s_lief == 1);
}
static void abschnitt_genommen_ohne_fx(void) { abschnitt_genommen(false, ACK, "Genommen ohne Animation (M3)"); }
static void abschnitt_genommen_mit_fx(void) { abschnitt_genommen(true, ACK, "Genommen mit Animation (M3)"); }
static void abschnitt_genommen_frist(void) { abschnitt_genommen(false, KEINE_ANTWORT, "Genommen, Telefon antwortet nicht (M3)"); }
static void abschnitt_genommen_offline(void) { abschnitt_genommen(true, OHNE_VERBINDUNG, "Genommen ohne Verbindung (M3)"); }

// Jeder Abschnitt laeuft in einem eigenen Prozess: frische statische
// Variablen wie bei jedem Start auf der Uhr.
static void (*const ABSCHNITTE[])(void) = {
  abschnitt_neuplanen, abschnitt_offen, abschnitt_genommen_ohne_fx, abschnitt_genommen_mit_fx,
  abschnitt_genommen_frist, abschnitt_genommen_offline,
};

int main(void) {
  printf("Zeitzone: %s\n", getenv("TZ") ? getenv("TZ") : "(Rechner)");
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
