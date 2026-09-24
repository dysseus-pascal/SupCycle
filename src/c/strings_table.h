// Alle Texte der Oberflaeche, eine Zeile je Text.
//
// ACHTUNG, ZWEI DINGE SIND ABSICHT:
//  1. KEIN #pragma once und keine Include-Waechter. Diese Datei wird MEHRFACH
//     eingebunden (X-Makro): einmal fuer die Aufzaehlung der Schluessel in
//     strings.h und einmal fuer die Tabelle in strings.c.
//  2. Endung .h, obwohl es kein gewoehnlicher Header ist. Build-Umgebungen,
//     die nur .c und .h in ihren Baum kopieren, faenden sie sonst nicht.
//
//   STR(schluessel, maxbytes, en, de, fr, it, es)
//
//   maxbytes  Groesse des Zielpuffers in BYTES, 0 wenn der Text in keinen
//             festen Puffer kopiert wird. Umlaute und Akzente zaehlen als
//             zwei Bytes, und ein Byte braucht die abschliessende Null.
//             tools/strings_check.js prueft diese Grenze.
//   en        Englisch, zugleich der Rueckfall fuer jede andere Sprache.
//   de, fr, it, es  Deutsch, Franzoesisch, Italienisch, Spanisch - in der
//             Reihenfolge von StringLang in strings.h.

// --- Hauptschirm ---
STR(STR_ALL_DONE,     0,  "All taken",          "Alles genommen", "Tout est pris", "Tutto preso", "Todo tomado")
STR(STR_NOTHING_DUE,  0,  "Nothing due today",  "Heute nichts fällig", "Rien aujourd'hui", "Niente per oggi", "Nada para hoy")
STR(STR_NO_PLAN,      0,  "No plan yet",        "Noch kein Plan", "Pas encore de plan", "Ancora nessun piano", "Aún no hay plan")
STR(STR_NO_PLAN_SUB,  0,  "Enter your supplements in the app settings on your phone.", "Trag deine Präparate in den App-Einstellungen auf dem Telefon ein.", "Saisis tes compléments dans les réglages de l'app sur le téléphone.", "Inserisci i tuoi integratori nelle impostazioni dell'app sul telefono.", "Añade tus suplementos en los ajustes de la app en el teléfono.")
STR(STR_OPEN_FMT,    32,  "%d of %d open",      "%d von %d offen", "%d sur %d à prendre", "%d di %d da prendere", "%d de %d pendientes")

// --- Zyklus-Seite ---
STR(STR_CYCLE,        0,  "Cycle",              "Zyklus", "Cycle", "Ciclo", "Ciclo")
STR(STR_DAILY,        0,  "daily",              "täglich", "tous les jours", "ogni giorno", "a diario")
STR(STR_EVERY_FMT,   32,  "every %d days",      "alle %d Tage", "tous les %d jours", "ogni %d giorni", "cada %d días")
STR(STR_UNLIMITED,    0,  "no break",           "ohne Pause", "sans pause", "senza pausa", "sin pausa")
STR(STR_ON_FMT,      32,  "week %d of %d",      "Woche %d von %d", "semaine %d/%d", "settimana %d/%d", "semana %d de %d")
// In der Pause zaehlt nicht, welche Pausenwoche laeuft, sondern wann sie
// endet - und die Zeile brach mit beidem auf dem Schirm ab.
STR(STR_PAUSE,        0,  "break",              "Pause", "pause", "pausa", "pausa")
STR(STR_DAYS_LEFT,   32,  "%d days to go",      "noch %d Tage", "encore %d jours", "ancora %d giorni", "faltan %d días")
STR(STR_DAY_LEFT,    32,  "1 day to go",        "noch 1 Tag", "encore 1 jour", "ancora 1 giorno", "falta 1 día")

// --- Tastenhinweise in der Seitenleiste (kurz! die Leiste ist schmal) ---
STR(STR_HINT_TAKE,   14,  "Take",               "Nehmen", "Prendre", "Prendi", "Tomar")
STR(STR_HINT_UNDO,   14,  "Undo",               "Zurück", "Annuler", "Annulla", "Anular")
STR(STR_HINT_BACK,   14,  "Today",              "Heute", "Retour", "Oggi", "Hoy")

// --- Erinnerung ---
STR(STR_SNOOZE_FMT,  24,  "Snoozed %d of %d",   "Aufschub %d von %d", "Report %d sur %d", "Rinvio %d di %d", "Aplazado %d de %d")
STR(STR_SNOOZE_LAST, 24,  "Last snooze",        "Letzter Aufschub", "Dernier report", "Ultimo rinvio", "Último aplazamiento")

// --- App-Glance im Starter ---
STR(STR_GLANCE_OPEN, 48,  "%d of %d still open", "%d von %d noch offen", "%d sur %d encore à prendre", "%d di %d ancora da prendere", "%d de %d aún pendientes")
STR(STR_GLANCE_DONE, 48,  "All taken today",    "Heute alles genommen", "Tout pris aujourd'hui", "Tutto preso oggi", "Todo tomado hoy")
STR(STR_GLANCE_NONE, 48,  "No plan yet",        "Noch kein Plan", "Pas encore de plan", "Ancora nessun piano", "Aún no hay plan")
