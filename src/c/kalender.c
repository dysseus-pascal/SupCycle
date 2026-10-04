#include "kalender.h"

int32_t kalender_tag_aus_datum(int jahr, int monat, int tag) {
  // Das Jahr beginnt hier im Maerz: der Schalttag liegt dann am Ende, und
  // jeder Monat davor hat eine feste Laenge. Eine Aera sind 400 Jahre.
  const int j = jahr - (monat <= 2 ? 1 : 0);
  const int aera = (j >= 0 ? j : j - 399) / 400;
  const int jahr_der_aera = j - aera * 400;
  const int tag_des_jahres = (153 * (monat + (monat > 2 ? -3 : 9)) + 2) / 5 + tag - 1;
  const int tag_der_aera = jahr_der_aera * 365 + jahr_der_aera / 4 - jahr_der_aera / 100 +
                           tag_des_jahres;
  // 719468 Tage liegen zwischen dem 01.03.0000 und dem 01.01.1970.
  return (int32_t)aera * 146097 + tag_der_aera - 719468;
}

int32_t kalender_tag(time_t t) {
  const struct tm *lt = localtime(&t);
  return kalender_tag_aus_datum(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
}

// Ortszeit von `t` als Minuten seit dem 01.01.1970 00:00 Ortszeit.
static int64_t prv_ortsminute(time_t t) {
  const struct tm *lt = localtime(&t);
  const int32_t tag = kalender_tag_aus_datum(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
  return (int64_t)tag * 1440 + lt->tm_hour * 60 + lt->tm_min;
}

time_t kalender_zeit_am(int32_t tag, int minute) {
  // Ohne mktime (das SDK hat keines): schaetzen, nachsehen, korrigieren.
  // Erster Schaetzwert ist die Uhrzeit in UTC; nach einer Runde steht die
  // Zeitzone drin, nach der zweiten auch eine Umstellung dazwischen.
  const int64_t soll = (int64_t)tag * 1440 + minute;
  time_t t = (time_t)tag * 86400 + (time_t)minute * 60;
  // Die frueheste Zeit, die schon NACH der gesuchten liegt: in der
  // Fruehjahrsluecke pendelt die Schaetzung zwischen davor und danach, und
  // dann gilt danach.
  time_t danach = 0;
  bool gibt_danach = false;
  for (int runde = 0; runde < 4; runde++) {
    const int64_t ist = prv_ortsminute(t);
    if (ist == soll) {
      // Zonen mit Sekundenversatz (historische Ortszeiten) landen sonst
      // mitten in der Minute.
      return t - localtime(&t)->tm_sec;
    }
    if (ist > soll && (!gibt_danach || t < danach)) {
      danach = t;
      gibt_danach = true;
    }
    t += (time_t)((soll - ist) * 60);
  }
  return gibt_danach ? danach : t;
}
