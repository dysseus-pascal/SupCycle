#include "remind.h"
#include "kalender.h"
#include "plan.h"

#define MAX_WAKEUPS 8
// Wakeups muessen in der Zukunft liegen (pebbleos lehnt time_difference <= 0
// ab). Zwei Sekunden reichen fuer die Zeit zwischen time() und dem Stellen.
// Bis sc-r2 waren es 30: jedes Neustellen in den 30 s vor einem Wecker schob
// ihn um bis zu 31 s, wiederholt bei jedem weiteren Neustellen.
#define LEAD_S      2
// So lange nach seiner Zeit wird ein Wecker, der noch nicht geklopft hat,
// sofort neu gestellt - siehe prv_noch_nicht_geklopft.
#define NACHHOLEN_S 60
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
// plan.c belegt 1..3, 8 und 9, prefs.c 4, remind.c 5..7 und 10..12.
#define PERSIST_SNOOZE_AT     5
#define PERSIST_SNOOZE_MINUTE 6
#define PERSIST_SNOOZE_COUNT  7
// Der Tag der aufgeschobenen Runde. Der Zaehler gilt nur an diesem Tag: die
// Runde 08:00 von morgen ist ein neuer Anlass und faengt bei null an.
#define PERSIST_SNOOZE_TAG    10
// Die Plaetze, die der Aufschub wiederbringt (je Platz ein Bit): im unteren
// Byte die vom Tag seiner Runde, im zweiten die vom Tag davor. Ueber
// Mitternacht kennt die Uhr die Haken von gestern nicht mehr (plan.c) - ohne
// diese Liste zeigte der Aufschub von 23:50 um 00:05 auch, was um 23:55 auf
// dem Heute-Schirm abgehakt wurde. Bis Mitternacht schrumpft sie mit jedem
// Haken (remind_schedule), danach steht sie fest.
#define PERSIST_SNOOZE_PLAETZE 11
// Bis wohin die Runden geklopft haben: die Zeit der letzten Runde, deren
// Wecker klopfte (hoechstens die Zeit des Klopfens). Siehe
// prv_noch_nicht_geklopft.
#define PERSIST_GEKLOPFT 12

static time_t prv_snooze_at(void) {
  return persist_exists(PERSIST_SNOOZE_AT) ? (time_t)persist_read_int(PERSIST_SNOOZE_AT) : 0;
}
static int prv_snooze_minute(void) {
  return persist_exists(PERSIST_SNOOZE_MINUTE) ? persist_read_int(PERSIST_SNOOZE_MINUTE) : -1;
}
static bool prv_snooze_tag(int32_t *tag) {
  if (!persist_exists(PERSIST_SNOOZE_TAG)) return false;
  *tag = (int32_t)persist_read_int(PERSIST_SNOOZE_TAG);
  return true;
}
// Ohne das Fach (Aufschub von sc-r und frueher) false: dann gilt die ganze
// Runde seiner Uhrzeit, wie damals.
static bool prv_snooze_plaetze(uint8_t *plaetze, uint8_t *vortag) {
  if (!persist_exists(PERSIST_SNOOZE_PLAETZE)) return false;
  const int32_t wert = persist_read_int(PERSIST_SNOOZE_PLAETZE);
  *plaetze = (uint8_t)(wert & 0xFF);
  *vortag = (uint8_t)((wert >> 8) & 0xFF);
  return true;
}
static void prv_snooze_plaetze_schreiben(uint8_t plaetze, uint8_t vortag) {
  persist_write_int(PERSIST_SNOOZE_PLAETZE, (int32_t)plaetze | ((int32_t)vortag << 8));
}

// Wie oft die Runde `minute` vom Kalendertag `tag` schon aufgeschoben wurde.
static int prv_zahl(int minute, int32_t tag) {
  if (minute < 0 || prv_snooze_minute() != minute) return 0;
  // Ohne Tag (Aufschub aus 0.15.0 und frueher) zaehlt er nicht - lieber
  // einmal zu viel aufschieben als eine Runde vorzeitig verfallen lassen.
  int32_t gemerkt;
  if (!prv_snooze_tag(&gemerkt) || gemerkt != tag) return 0;
  return persist_exists(PERSIST_SNOOZE_COUNT) ? persist_read_int(PERSIST_SNOOZE_COUNT) : 0;
}

int32_t remind_runden_tag(int32_t cookie) {
  // Gestern ist es nur bei einem Aufschub, der sich den Tag seiner Runde
  // gemerkt hat. NICHT nach der Uhrzeit: nach einer Reise nach Westen klopft
  // der Wecker von 08:00 um 02:00 Ortszeit, und ein liegen gebliebener
  // Aufschub derselben Uhrzeit vom Vortag machte daraus die Runde von
  // gestern - "Genommen" hakte nichts ab (Review sc-r2).
  int32_t gemerkt;
  if (remind_cookie_aufschub(cookie) && prv_snooze_minute() == remind_cookie_minute(cookie) &&
      prv_snooze_tag(&gemerkt)) {
    return gemerkt;
  }
  return plan_today();
}

int remind_snooze_count(int minute, int32_t tag) {
  return prv_zahl(minute, tag);
}
bool remind_snooze_left(int minute, int32_t tag) {
  return remind_snooze_count(minute, tag) < SC_SNOOZE_MAX;
}
void remind_snooze_clear(int minute) {
  // NUR DER AUFSCHUB DIESER RUNDE. Bis 0.15.0 loeschte jedes Abhaken und
  // Wegdruecken den einen gemerkten Aufschub, gleich welcher Runde er galt:
  // Morgenrunde 08:00 aufgeschoben, um 08:10 eine andere Runde abgehakt -
  // und 08:00 kam nie wieder.
  const int gemerkt = prv_snooze_minute();
  if (minute >= 0 && gemerkt >= 0 && gemerkt != minute) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Aufschub der Minute %d bleibt", gemerkt);
    return;
  }
  persist_delete(PERSIST_SNOOZE_AT);
  persist_delete(PERSIST_SNOOZE_MINUTE);
  persist_delete(PERSIST_SNOOZE_COUNT);
  persist_delete(PERSIST_SNOOZE_TAG);
  persist_delete(PERSIST_SNOOZE_PLAETZE);
}
void remind_snooze(const Aufschub *a) {
  const int count = prv_zahl(a->minute, a->tag) + 1;
  persist_write_int(PERSIST_SNOOZE_AT, (int)(time(NULL) + SC_SNOOZE_MIN * 60));
  persist_write_int(PERSIST_SNOOZE_MINUTE, a->minute);
  persist_write_int(PERSIST_SNOOZE_COUNT, count);
  persist_write_int(PERSIST_SNOOZE_TAG, (int)a->tag);
  prv_snooze_plaetze_schreiben(a->plaetze, a->plaetze_vortag);
  APP_LOG(APP_LOG_LEVEL_INFO, "Aufschub %d fuer Minute %d vom Tag %d, Plaetze %02x/%02x", count, a->minute,
          (int)a->tag, a->plaetze, a->plaetze_vortag);
  remind_schedule();
}
bool remind_aufschub(Aufschub *a) {
  const int minute = prv_snooze_minute();
  if (minute < 0 || !prv_snooze_tag(&a->tag) || !prv_snooze_plaetze(&a->plaetze, &a->plaetze_vortag)) {
    return false;
  }
  a->minute = minute;
  return true;
}

// Der Tag der Runde, der der gemerkte Aufschub gilt. Ohne gemerkten Tag
// (0.15.0) der letzte Tag, an dem ihre Uhrzeit vor dem Klopfen schon da war;
// ohne beides heute.
static int32_t prv_snooze_runde(time_t snooze_at, int minute) {
  int32_t tag;
  if (prv_snooze_tag(&tag)) return tag;
  if (snooze_at <= 0) return plan_today();
  tag = kalender_tag(snooze_at);
  if (minute >= 0 && kalender_zeit_am(tag, minute) > snooze_at) tag--;
  return tag;
}

// Wann ein Wecker fuer `t` zu stellen ist: `t`, aber nicht vor LEAD_S.
// remind_schedule faengt mit wakeup_cancel_all an - was in den naechsten
// LEAD_S Sekunden geklopft haette, ist damit abgesagt. Bis 0.15.0 wurde es
// dann gar nicht mehr gestellt: wer die App um 08:14:45 oeffnete, bekam den
// Aufschub von 08:15 nie (und um 07:59:45 die Runde von 08:00 nie). Jetzt
// klopft er eben ein paar Sekunden spaeter.
static time_t prv_fruehestens(time_t t, time_t now) {
  return t > now + LEAD_S ? t : now + LEAD_S + 1;
}

// HAT DIE RUNDE UM `at` SCHON GEKLOPFT? Nicht aus der Uhrzeit zu schliessen:
// ein Wecker, den das Neustellen kurz vor seiner Zeit ein paar Sekunden nach
// hinten schob, ist bei "at <= now" noch nicht dagewesen. Bis sc-r2 fiel er
// dann beim naechsten Neustellen weg - ein Haken auf dem Heute-Schirm um
// 08:00:05, und die Runde 08:00 klopfte heute nicht mehr. Nachgeholt wird nur
// bis NACHHOLEN_S nach ihrer Zeit; was die Uhr laenger verpasste (aus), meldet
// sie selbst.
static bool prv_noch_nicht_geklopft(time_t at, time_t now) {
  const time_t geklopft = persist_exists(PERSIST_GEKLOPFT) ? (time_t)persist_read_int(PERSIST_GEKLOPFT) : 0;
  return at > geklopft && at > now - NACHHOLEN_S;
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

// Kann je wieder etwas faellig werden, ohne dass der Plan sich aendert? Nein
// bei leerem Plan und wenn jede Kur ohne Pause abgelaufen ist (CyclePhaseDone
// bleibt Done). Dann nuetzt der Wecker zum Neuplanen nichts: ein neuer Plan
// kommt nur bei offener App, und der Neuplan-Start fragt das Telefon nicht.
// Er verdraengte nur jede Nacht, was um 03:00 im Vordergrund laeuft.
static bool prv_wird_wieder_faellig(void) {
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    const PlanItem *it = plan_item(i);
    if (it && it->used && plan_cycle(i).phase != CyclePhaseDone) return true;
  }
  return false;
}

// Ist vom gemerkten Aufschub (Runde `minute` vom Kalendertag `runde`) noch
// etwas offen? Mit Plaetzen: die davon, die an ihrem Tag noch anstehen und -
// heute - nicht abgehakt sind; dabei schrumpft die Liste mit jedem Haken, denn
// nach Mitternacht weiss die Uhr nichts mehr davon. Die Plaetze vom Vortag
// zaehlen, solange der Vortag gestern ist. Ohne Plaetze (Aufschub von sc-r
// und frueher) die ganze Runde seiner Uhrzeit.
static bool prv_aufschub_offen(int32_t runde, int minute) {
  const bool heute = runde == plan_today();
  uint8_t plaetze, vortag;
  if (!prv_snooze_plaetze(&plaetze, &vortag)) return prv_noch_offen(runde, minute, heute);
  uint8_t noch = 0, noch_vortag = 0;
  for (int i = 0; i < SC_MAX_ITEMS; i++) {
    const uint8_t bit = (uint8_t)(1u << i);
    if ((plaetze & bit) && plan_due_on(i, runde) && !(heute && plan_taken(i))) noch |= bit;
    if ((vortag & bit) && heute && plan_due_on(i, runde - 1)) noch_vortag |= bit;
  }
  if (heute && noch != plaetze) prv_snooze_plaetze_schreiben(noch, vortag);
  return noch != 0 || noch_vortag != 0;
}

void remind_schedule(void) {
  const time_t now = time(NULL);
  wakeup_cancel_all();
  int n = 0;

  // Der offene Aufschub zuerst - aber nur, solange seine Runde noch offen
  // ist. Er gilt der Runde von heute oder der von gestern: 23:50
  // aufgeschoben klopft um 00:05 (bis 0.15.0 nur am selben Tag, und so
  // klopfte ein Aufschub ueber Mitternacht nie). Aelter wird keiner.
  // Der frueheste Wecker, der die App oeffnet und damit neu plant (Aufschub
  // oder Erinnerung); 0 = keiner.
  time_t erste = 0;
  const time_t snooze_at = prv_snooze_at();
  const int snooze_minute = prv_snooze_minute();
  if (snooze_minute >= 0) {
    const int32_t heute = plan_today();
    const int32_t runde = prv_snooze_runde(snooze_at, snooze_minute);
    const int32_t klopft = kalender_tag(snooze_at);
    const bool gestern = runde == heute - 1;
    // Die Haken von gestern kennt die Uhr nach Mitternacht nicht mehr
    // (plan.c). Ob die Runde von gestern noch offen ist, steht deshalb schon
    // vor Mitternacht fest - siehe den ersten Zweig und prv_aufschub_offen.
    const bool offen = prv_aufschub_offen(runde, snooze_minute);
    // Sein Wecker steht im Persist, bis er geklopft hat (remind_geklopft) -
    // nicht, bis seine Zeit vorbei ist (siehe prv_noch_nicht_geklopft).
    const bool verpasst = snooze_at > 0 && snooze_at <= now - NACHHOLEN_S;
    const bool gilt = snooze_at > 0 && !verpasst && offen && (runde == heute || gestern) &&
        (klopft == runde || klopft == runde + 1);
    if (runde == heute && !offen) {
      // HEUTE ERLEDIGT (etwa auf dem Heute-Schirm): der ganze Aufschub ist
      // vorbei, nicht nur sein Wecker. Bis sc-r2 blieben Minute, Zaehler und
      // Tag stehen. Klopfte er erst nach Mitternacht, wuesste die Uhr nicht
      // mehr, dass die Runde genommen ist, und erinnerte an Erledigtes.
      APP_LOG(APP_LOG_LEVEL_INFO, "Runde %d erledigt - Aufschub vorbei", snooze_minute);
      remind_snooze_clear(-1);
    } else if (gilt) {
      const time_t wann = prv_fruehestens(snooze_at, now);
      if (prv_schedule(wann, COOKIE_SNOOZE + snooze_minute, true)) {
        n++;
        erste = wann;
      }
    } else if (verpasst) {
      // Die Uhr verpasste ihn (aus, Wecker verworfen) und meldet das selbst.
      // Vorbei ist nur der WECKER. Den Zaehler loeschen erst Abhaken und
      // Wegdruecken - bis 0.15.0 fiel er mit weg, und jeder Aufschub war
      // wieder der erste: "hoechstens dreimal" griff nie.
      APP_LOG(APP_LOG_LEVEL_INFO, "Aufschub der Minute %d verpasst", snooze_minute);
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
  // eine Stunde daneben liegt (Audit M2). Hinter SC_VORAUS_TAGE nur, solange
  // noch keine Erinnerung gefunden ist: dann bis zum ersten faelligen Tag.
  const int32_t heute = plan_today();
  int gefunden = 0;
  for (int day = 0; day < SC_SUCHE_TAGE && n < MAX_WAKEUPS - 1; day++) {
    if (day >= SC_VORAUS_TAGE && gefunden > 0) break;
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
      // Heute schon vorbei - ausser sie hat noch nicht geklopft.
      if (at <= now && !prv_noch_nicht_geklopft(at, now)) continue;
      gefunden++;
      const time_t wann = prv_fruehestens(at, now);
      if (prv_schedule(wann, best, true)) {
        n++;
        if (erste == 0 || wann < erste) erste = wann;
      }
    }
  }
  if (gefunden == 0) {
    APP_LOG(APP_LOG_LEVEL_INFO, "In %d Tagen keine Erinnerung faellig", SC_SUCHE_TAGE);
  }

  // Zuletzt der Wecker zum Neuplanen, auf den letzten Platz: die naechste
  // SC_NEUPLANEN_MINUTE, die noch kommt. Die Erinnerungen behalten so ihre
  // Zeiten; kaeme er einer in die Quere, weicht er aus (prv_schedule).
  time_t neu = kalender_zeit_am(heute, SC_NEUPLANEN_MINUTE);
  if (neu <= now + LEAD_S) neu = kalender_zeit_am(heute + 1, SC_NEUPLANEN_MINUTE);
  // NICHT GEGEN DAS 03:00 VON MORGEN, sondern gegen das danach: nach der
  // letzten Runde des Abends liegt die naechste Erinnerung (morgen frueh)
  // immer hinter dem naechsten 03:00. Mit diesem Vergleich stuende der Wecker
  // bei taeglichen Praeparaten wieder jede Nacht.
  const time_t danach = kalender_zeit_am(kalender_tag(neu) + 1, SC_NEUPLANEN_MINUTE);
  if (erste > 0 && erste <= danach) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Kein Wecker zum Neuplanen - die naechste Erinnerung plant neu");
  } else if (!prv_wird_wieder_faellig()) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Kein Wecker zum Neuplanen - nichts wird wieder faellig");
  } else if (prv_schedule(neu, COOKIE_NEUPLANEN, false)) {
    n++;
  }
  APP_LOG(APP_LOG_LEVEL_INFO, "%d Wecker gestellt", n);
}

int remind_cookie_minute(int32_t cookie) {
  if (remind_cookie_aufschub(cookie)) {
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

bool remind_cookie_aufschub(int32_t cookie) {
  return cookie >= COOKIE_SNOOZE && cookie < COOKIE_SNOOZE + 1440;
}

void remind_geklopft(int32_t cookie) {
  const int minute = remind_cookie_minute(cookie);
  if (minute < 0) return;
  if (remind_cookie_aufschub(cookie)) {
    // Sein Wecker ist vorbei; Zaehler und Plaetze bleiben fuer das Fenster.
    if (prv_snooze_minute() == minute) {
      persist_delete(PERSIST_SNOOZE_AT);
    } else {
      APP_LOG(APP_LOG_LEVEL_WARNING, "Aufschub %d klopfte, gemerkt ist %d", minute, prv_snooze_minute());
    }
    return;
  }
  // Die Zeit der Runde, aber nicht spaeter als jetzt: nach einer Reise nach
  // Westen klopft der Wecker vor ihr, und Runden dazwischen klopften nicht.
  const time_t jetzt = time(NULL);
  time_t runde = kalender_zeit_am(plan_today(), minute);
  if (runde > jetzt) runde = jetzt;
  persist_write_int(PERSIST_GEKLOPFT, (int)runde);
}

bool remind_launch_cookie(int32_t *cookie) {
  if (launch_reason() != APP_LAUNCH_WAKEUP) return false;
  WakeupId id;
  return wakeup_get_launch_event(&id, cookie);
}

