#include <pebble.h>
#include "cycle_selftest.h"
#include "main_window.h"
#include "phone.h"
#include "plan.h"
#include "remind.h"
#include "reminder_window.h"
#include "strings.h"

// App-Glance im Starter: was heute noch offen ist, ohne die App zu oeffnen.
static void prv_glance_reload(AppGlanceReloadSession *session, size_t limit, void *context) {
  if (limit < 1) return;
  char text[48];
  if (plan_count() == 0) {
    snprintf(text, sizeof(text), "%s", S(STR_GLANCE_NONE));
  } else {
    const int open = plan_open_today();
    int due = 0;
    for (int i = 0; i < SC_MAX_ITEMS; i++) {
      if (plan_due_today(i)) due++;
    }
    if (open == 0) snprintf(text, sizeof(text), "%s", S(STR_GLANCE_DONE));
    else snprintf(text, sizeof(text), S(STR_GLANCE_OPEN), open, due);
  }
  const AppGlanceSlice slice = {
    .layout = { .icon = APP_GLANCE_SLICE_DEFAULT_ICON, .subtitle_template_string = text },
    .expiration_time = APP_GLANCE_SLICE_NO_EXPIRATION,
  };
  app_glance_add_slice(session, slice);
}

// Ein neuer Plan aendert, wann und ob erinnert wird - die Wecker muessen mit.
static void prv_plan_changed(void) {
  main_window_refresh();
  remind_schedule(0);
}

static void prv_init(void) {
  // Sprache der Uhr uebernehmen, bevor das erste Fenster Texte holt
  strings_refresh();

#ifdef SC_SELFTEST
  // Nur im Pruefbau: die Zyklusrechnung gegen vorab bestimmte Erwartungen
  // stellen. Steht bewusst VOR dem ersten Fenster, damit das Log vollstaendig
  // ist, auch wenn die App danach sofort beendet wird.
  cycle_selftest_run();
#endif

  plan_init();
  phone_init();
  phone_set_observer(prv_plan_changed);
  main_window_push();

  // Hat uns ein Wecker geoeffnet, sofort erinnern. Das Fenster legt sich ueber
  // den Hauptschirm; ist nichts mehr offen, erscheint es gar nicht.
  const int minute = remind_launch_minute();
  if (minute != -1) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Vom Wecker geoeffnet (Minute %d)", minute);
    reminder_window_push(minute);
  }

  // Wecker bei JEDEM Start neu stellen: so haelt sich der Weckplan selbst
  // aktuell, auch nach einem Neustart der Uhr, einer Zeitumstellung oder einem
  // Zyklus, der ueber Nacht in die Pause gewechselt ist.
  remind_schedule(0);
}

static void prv_deinit(void) {
  app_glance_reload(prv_glance_reload, NULL);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
  return 0;
}
