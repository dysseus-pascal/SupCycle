#include "remind.h"
#include "plan.h"

#define MAX_WAKEUPS 8
#define LEAD_S      30    // Wakeups muessen etwas in der Zukunft liegen
#define COOKIE_SNOOZE 1440  // ausserhalb des Minutenbereichs 0..1439

// Wakeups brauchen eine Minute Abstand zueinander. Zwei Praeparate zur selben
// Uhrzeit ergeben aber nur EINEN Wecker - das Erinnerungsfenster zeigt dann
// beide. Bei E_RANGE trotzdem bis zu zweimal um je zwei Minuten nach hinten
// schieben, wie in Drinktervall.
static bool prv_schedule(time_t t, int32_t cookie) {
  for (int attempt = 0; attempt < 3; attempt++) {
    WakeupId id = wakeup_schedule(t + attempt * 120, cookie, true);
    if (id >= 0) return true;
    if (id != E_RANGE) return false;
  }
  return false;
}

// Mitternacht des Tages, in dem `t` liegt. Ohne mktime, wie in den
// Schwesterapps.
static time_t prv_midnight(time_t t) {
  struct tm *lt = localtime(&t);
  return t - (lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec);
}

void remind_schedule(time_t snooze_at) {
  const time_t now = time(NULL);
  wakeup_cancel_all();
  int n = 0;

  if (snooze_at > now + LEAD_S && prv_schedule(snooze_at, COOKIE_SNOOZE)) n++;

#ifdef SC_TEST_WAKE
  // Pruefbau: in einer Minute wecken, mit der Uhrzeit des ersten heute noch
  // offenen Praeparats als Cookie. Nur so laesst sich im Emulator nachsehen,
  // ob der Wecker die App wirklich oeffnet und das richtige Fenster zeigt -
  // sonst muesste man bis zur echten Uhrzeit warten.
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    const PlanItem *it = plan_item(i);
    if (!it || !plan_due_today(i) || plan_taken(i)) continue;
    if (prv_schedule(now + 60, it->hour * 60 + it->minute)) n++;
    break;
  }
  APP_LOG(APP_LOG_LEVEL_INFO, "Pruefbau: Wecker in einer Minute");
  return;
#endif

  // Zwei Tage vorausplanen. Mehr braucht es nicht: bei jedem Start wird neu
  // gestellt, und acht Wecker reichen ohnehin nicht weiter.
  const time_t midnight = prv_midnight(now);
  for (int day = 0; day < 2 && n < MAX_WAKEUPS; day++) {
    const int32_t the_day = (int32_t)((midnight + day * 86400) / 86400);

    // Je Minute des Tages hoechstens ein Wecker, auch wenn mehrere Praeparate
    // darauf fallen. Deshalb aufsteigend durchgehen und Doppelte ueberspringen.
    int last_minute = -1;
    for (int pass = 0; pass < SC_MAX_ITEMS && n < MAX_WAKEUPS; pass++) {
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

      const time_t at = midnight + day * 86400 + (time_t)best * 60;
      if (at <= now + LEAD_S) continue;     // heute schon vorbei
      if (prv_schedule(at, best)) n++;
    }
  }
  APP_LOG(APP_LOG_LEVEL_INFO, "%d Wecker gestellt", n);
}

int remind_launch_minute(void) {
  if (launch_reason() != APP_LAUNCH_WAKEUP) return -1;
  WakeupId id;
  int32_t cookie;
  if (!wakeup_get_launch_event(&id, &cookie)) return -1;
  if (cookie == COOKIE_SNOOZE) {
    // Nach einem Aufschub gilt wieder, was gerade offen ist.
    return -2;
  }
  if (cookie < 0 || cookie > 1439) return -1;
  return (int)cookie;
}
