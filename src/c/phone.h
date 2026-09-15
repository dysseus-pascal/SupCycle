#pragma once
#include <pebble.h>

// Die Telefonseite schickt den Plan. Mehr kommt von dort nicht, und die Uhr
// schickt nichts zurueck - sie fragt beim Start einmal an, damit ein frisch
// aufgespieltes Paket nicht ohne Plan dasteht.
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
