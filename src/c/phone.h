#pragma once
#include <pebble.h>

// Die Telefonseite schickt den Plan. Die Uhr fragt beim Start einmal an,
// damit ein frisch aufgespieltes Paket nicht ohne Plan dasteht.
//
// Boulder schickt ausserdem den Befehl, Haken zurueckzunehmen (UNTAKE mit
// TODAY und TAKEN_AT): wer die Einnahme dort loescht, soll sie auf der Uhr
// nicht weiter als genommen sehen. Eine aeltere Fassung ueberhoert ihn.
//
// KEIN companionApp-Eintrag in package.json: der schaltete die Pebble-App auf
// PebbleKit2 um, und damit die pkjs-Seite ab. Hier laeuft alles ueber pkjs.
void phone_init(void);

// Wird gerufen, wenn ein neuer Plan angekommen ist.
void phone_set_observer(void (*on_plan)(void));

// Dem Telefon sagen, was HEUTE ansteht und was davon schon genommen ist.
// Daraus baut die Telefonseite die Timeline-Pins.
//
// Geschickt werden nur der Tag und zwei Bitmasken - die Namen und Uhrzeiten
// hat die Telefonseite ohnehin, sie hat den Plan ja selbst gebaut. Und die
// ZYKLUSRECHNUNG bleibt damit an einer einzigen Stelle: in cycle.c. Sie in
// JavaScript nachzubauen hiesse, zwei Wahrheiten zu pflegen, die
// auseinanderlaufen koennen.
void phone_send_today(void);

// `fertig` rufen, sobald die Tagesmeldung erledigt ist: das Telefon hat sie
// bestaetigt oder abgelehnt, oder sie ging gar nicht erst hinaus - spaetestens
// aber nach `max_ms`. Steht nichts aus, sofort.
//
// WOZU: wer die App nach dem Abhaken schliesst, beendet auch die Meldung.
// Die Uhr verwirft beim Beenden, was noch im Postausgang liegt, und ein
// Nachfassen nach BUSY stirbt mit der App (Audit M3). Der Haken bleibt zwar im
// Persist, aber faellt der naechste Start auf den naechsten Tag, ist er weg,
// bevor das Telefon ihn kennt.
void phone_when_sent(void (*fertig)(void), uint32_t max_ms);
