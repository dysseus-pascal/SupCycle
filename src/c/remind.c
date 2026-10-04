#include "remind.h"
#include "kalender.h"
#include "plan.h"

#define MAX_WAKEUPS 8
#define LEAD_S      30    // Wakeups muessen etwas in der Zukunft liegen
// Ein Aufschub traegt die Uhrzeit seiner Runde im Cookie, um COOKIE_SNOOZE
// verschoben - ausserhalb des Minutenbereichs 0..1439, damit ihn nichts
// mit einem gewoehnlichen Wecker verwechselt.
#define COOKIE_SNOOZE 2000
// Der Wecker zum Neuplanen, ausserhalb beider Bereiche.
#define COOKIE_NEUPLANEN 5000

// Der offene Aufschub liegt im Persist: die App wird bei jedem Wecker neu
// gestartet, und beim Start werden alle Wecker neu gestellt. Ohne Persist
// fiele der Aufschub beim ersten Start dazwischen weg - genau das war der
// Fehler, mit dem ein "spaeter" am Morgen manchmal nie wiederkam.
// plan.c belegt 1..3, 8 und 9, prefs.c 4.
#define PERSIST_SNOOZE_AT     5
#define PERSIST_SNOOZE_MINUTE 6
#define PERSIST_SNOOZE_COUNT  7
// Der Tag der aufgeschobenen Runde. Der Zaehler gilt nur an diesem Tag: die
// Runde 08:00 von morgen ist ein neuer Anlass und faengt bei null an.
#define PERSIST_SNOOZE_TAG    10

static time_t prv_snooze_at(void) {
  return persist_exists(PERSIST_SNOOZE_AT) ? (time_t)persist_read_int(PERSIST_SNOOZE_AT) : 0;
}
static int prv_snooze_minute(void) {
  return persist_exists(PERSIST_SNOOZE_MINUTE) ? persist_read_int(PERSIST_SNOOZE_MINUTE) : -1;
}
int remind_snooze_count(int minute) {
  if (minute < 0 || prv_snooze_minute() != minute) return 0;
  // Ohne Tag (Aufschub aus 0.15.0 und frueher) zaehlt er nicht - lieber
  // einmal zu viel aufschieben als eine Runde vorzeitig verfallen lassen.
  if (!persist_exists(PERSIST_SNOOZE_TAG) || persist_read_int(PERSIST_SNOOZE_TAG) != plan_today()) return 0;
  return persist_exists(PERSIST_SNOOZE_COUNT) ? persist_read_int(PERSIST_SNOOZE_COUNT) : 0;
}
bool remind_snooze_left(int minute) {
  return remind_snooze_count(minute) < SC_SNOOZE_MAX;
}
void remind_snooze_clear(void) {
  persist_delete(PERSIST_SNOOZE_AT);
  persist_delete(PERSIST_SNOOZE_MINUTE);
  persist_delete(PERSIST_SNOOZE_COUNT);
  persist_delete(PERSIST_SNOOZE_TAG);
}
void remind_snooze(int minute) {
  const int count = remind_snooze_count(minute) + 1;
  persist_write_int(PERSIST_SNOOZE_AT, (int)(time(NULL) + SC_SNOOZE_MIN * 60));
  persist_write_int(PERSIST_SNOOZE_MINUTE, minute);
  persist_write_int(PERSIST_SNOOZE_COUNT, count);
  persist_write_int(PERSIST_SNOOZE_TAG, (int)plan_today());
  APP_LOG(APP_LOG_LEVEL_INFO, "Aufschub %d fuer Minute %d", count, minute);
  remind_schedule();
}

// Wakeups brauchen eine Minute Abstand zueinander. Zwei Praeparate zur selben
// Uhrzeit ergeben aber nur EINEN Wecker - das Erinnerungsfenster zeigt dann
// beide. Bei E_RANGE trotzdem bis zu zweimal um je zwei Minuten nach hinten
// schieben, wie in Drinktervall.
// `melden`: verpasst die Uhr ihn (aus, Neustart), sagt sie es beim Start.
static bool prv_schedule(time_t t, int32_t cookie, bool melden) {
  for (int attempt = 0; attempt < 3; attempt++) {
    WakeupId id = wakeup_schedule(t + attempt * 120, cookie, melden);
    if (id >= 0) return true;
    if (id != E_RANGE) {
      APP_LOG(APP_LOG_LEVEL_WARNING, "Wecker %d nicht gestellt: %d", (int)cookie, (int)id);
      return false;
    }
  }
  APP_LOG(APP_LOG_LEVEL_WARNING, "Wecker %d nicht gestellt: kein Platz", (int)cookie);
  return false;
}

/**
 * Ist zu dieser Minute ueberhaupt noch etwas offen?
 *
 * NUR FUER HEUTE eine Frage - morgen ist noch nichts abgehakt.
 *
 * Ohne diese Pruefung stand auf eine schon erledigte Runde weiter ein Wecker.
 * Er feuerte, oeffnete die App, das Erinnerungsfenster fand nichts zu zeigen
 * und erschien gar nicht - und zurueck blieb der HEUTE-SCHIRM. Dort steht
 * Abgehaktes durchgestrichen mit, und genau so sah es aus, als waere die
 * Morgenrunde am Mittag wieder faellig.
 *
 * prv_take im Erinnerungsfenster stellt die Wecker nach dem Abhaken neu und
 * schreibt dazu "die eben abgehakten sollen heute nicht nochmal klopfen".
 * Dieses Versprechen wurde hier nie eingeloest: der Weckplan sah nur, WANN
 * etwas faellig ist, nie OB es noch aussteht.
 */
static bool prv_noch_offen(int32_t the_day, int minute, bool heute) {
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    const PlanItem *it = plan_item(i);
    if (!it || !it->used) continue;
    if (it->hour * 60 + it->minute != minute) continue;
    if (!plan_due_on(i, the_day)) continue;
    if (!heute || !plan_taken(i)) return true;
  }
  return false;
}

void remind_schedule(void) {
  const time_t now = time(NULL);
  wakeup_cancel_all();
  int n = 0;

  // Der offene Aufschub zuerst - aber nur, solange seine Runde noch offen
  // ist und der Tag derselbe. Ein Aufschub von gestern klopft nicht heute.
  const time_t snooze_at = prv_snooze_at();
  const int snooze_minute = prv_snooze_minute();
  if (snooze_at > 0) {
    const bool gilt = snooze_at > now + LEAD_S && snooze_minute >= 0 &&
        kalender_tag(snooze_at) == kalender_tag(now) &&
        prv_noch_offen(plan_today(), snooze_minute, true);
    if (gilt) {
      if (prv_schedule(snooze_at, COOKIE_SNOOZE + snooze_minute, true)) n++;
    } else if (snooze_at <= now) {
      // Verstrichen: meist hat er eben geklopft, sonst verpasste ihn die Uhr
      // (aus, Wecker verworfen). Vorbei ist nur der WECKER. Den Zaehler
      // loeschen erst Abhaken und Wegdruecken - bis 0.15.0 fiel er hier mit
      // weg, und jeder Aufschub war wieder der erste: "hoechstens dreimal"
      // griff nie.
      persist_delete(PERSIST_SNOOZE_AT);
    }
  }

#ifdef SC_TEST_WAKE
  // Pruefbau: in einer Minute wecken, mit der Uhrzeit des ersten heute noch
  // offenen Praeparats als Cookie. Nur so laesst sich im Emulator nachsehen,
  // ob der Wecker die App wirklich oeffnet und das richtige Fenster zeigt -
  // sonst muesste man bis zur echten Uhrzeit warten.
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    const PlanItem *it = plan_item(i);
    if (!it || !plan_due_today(i) || plan_taken(i)) continue;
    if (prv_schedule(now + 60, it->hour * 60 + it->minute, true)) n++;
    break;
  }
  APP_LOG(APP_LOG_LEVEL_INFO, "Pruefbau: Wecker in einer Minute");
  return;
#endif

  // So weit voraus, bis die Erinnerungen gefunden sind - ein Platz bleibt
  // dem Wecker zum Neuplanen. Gerechnet in Kalendertagen; die Uhrzeit je Tag
  // kommt aus kalender_zeit_am, damit ein Wecker nach einer Umstellung nicht
  // eine Stunde daneben liegt (Audit M2).
  const int32_t heute = plan_today();
  for (int day = 0; day < SC_VORAUS_TAGE && n < MAX_WAKEUPS - 1; day++) {
    const int32_t the_day = heute + day;

    // Je Minute des Tages hoechstens ein Wecker, auch wenn mehrere Praeparate
    // darauf fallen. Deshalb aufsteigend durchgehen und Doppelte ueberspringen.
    int last_minute = -1;
    for (int pass = 0; pass < SC_MAX_ITEMS && n < MAX_WAKEUPS - 1; pass++) {
      int best = -1;
      for (int i = 0; i < SC_MAX_ITEMS; i++) {
        const PlanItem *it = plan_item(i);
        if (!it || !it->used) continue;
        // Nur woran an DIESEM Tag ueberhaupt zu erinnern ist. In der Pause
        // schweigt die Uhr - sonst hakt man aus Gewohnheit ab, und der Zyklus
        // ist wertlos.
        if (!plan_due_on(i, the_day)) continue;
        const int minute = it->hour * 60 + it->minute;
        if (minute <= last_minute) continue;
        if (best < 0 || minute < best) best = minute;
      }
      if (best < 0) break;
      last_minute = best;

      // Heute schon erledigt? Dann kein Wecker. Siehe prv_noch_offen.
      if (!prv_noch_offen(the_day, best, day == 0)) continue;

      const time_t at = kalender_zeit_am(the_day, best);
      if (at <= now + LEAD_S) continue;     // heute schon vorbei
      if (prv_schedule(at, best, true)) n++;
    }
  }

  // Zuletzt der Wecker zum Neuplanen, auf den letzten Platz: die naechste
  // SC_NEUPLANEN_MINUTE, die noch kommt. Die Erinnerungen behalten so ihre
  // Zeiten; kaeme er einer in die Quere, weicht er aus (prv_schedule).
  time_t neu = kalender_zeit_am(heute, SC_NEUPLANEN_MINUTE);
  if (neu <= now + LEAD_S) neu = kalender_zeit_am(heute + 1, SC_NEUPLANEN_MINUTE);
  if (prv_schedule(neu, COOKIE_NEUPLANEN, false)) n++;
  APP_LOG(APP_LOG_LEVEL_INFO, "%d Wecker gestellt", n);
}

int remind_cookie_minute(int32_t cookie) {
  if (cookie >= COOKIE_SNOOZE && cookie < COOKIE_SNOOZE + 1440) {
    // Ein Aufschub: die Erinnerung gilt der aufgeschobenen Runde, nicht
    // allem, was gerade offen ist.
    return (int)(cookie - COOKIE_SNOOZE);
  }
  if (cookie < 0 || cookie > 1439) return -1;
  return (int)cookie;
}

bool remind_cookie_neuplanen(int32_t cookie) {
  return cookie == COOKIE_NEUPLANEN;
}

bool remind_launch_cookie(int32_t *cookie) {
  if (launch_reason() != APP_LAUNCH_WAKEUP) return false;
  WakeupId id;
  return wakeup_get_launch_event(&id, cookie);
}

int remind_launch_minute(void) {
  int32_t cookie;
  if (!remind_launch_cookie(&cookie)) return -1;
  return remind_cookie_minute(cookie);
}
