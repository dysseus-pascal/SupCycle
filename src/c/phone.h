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
