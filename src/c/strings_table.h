// Alle Texte der Oberflaeche, eine Zeile je Text.
//
// ACHTUNG, ZWEI DINGE SIND ABSICHT:
//  1. KEIN #pragma once und keine Include-Waechter. Diese Datei wird MEHRFACH
//     eingebunden (X-Makro): einmal fuer die Aufzaehlung der Schluessel in
//     strings.h und einmal fuer die Tabelle in strings.c.
//  2. Endung .h, obwohl es kein gewoehnlicher Header ist. Build-Umgebungen,
//     die nur .c und .h in ihren Baum kopieren, faenden sie sonst nicht.
//
//   STR(schluessel, maxbytes, en, de)

// --- Hauptschirm ---
STR(STR_ALL_DONE,     0,  "All taken",          "Alles genommen")
STR(STR_NOTHING_DUE,  0,  "Nothing due today",  "Heute nichts fällig")
STR(STR_NO_PLAN,      0,  "No plan yet",        "Noch kein Plan")
STR(STR_NO_PLAN_SUB,  0,  "Enter your supplements in the app settings on your phone.", "Trag deine Präparate in den App-Einstellungen auf dem Telefon ein.")
STR(STR_OPEN_FMT,    32,  "%d of %d open",      "%d von %d offen")

// --- Zyklus-Seite ---
STR(STR_CYCLE,        0,  "Cycle",              "Zyklus")
STR(STR_DAILY,        0,  "daily",              "täglich")
STR(STR_TAKEN,        0,  "taken",              "genommen")
STR(STR_ON_FMT,      32,  "week %d of %d",      "Woche %d von %d")
// In der Pause zaehlt nicht, welche Pausenwoche laeuft, sondern wann sie
// endet - und die Zeile brach mit beidem auf dem Schirm ab.
STR(STR_PAUSE,        0,  "break",              "Pause")
STR(STR_DAYS_LEFT,   32,  "%d days to go",      "noch %d Tage")
STR(STR_DAY_LEFT,    32,  "1 day to go",        "noch 1 Tag")

// --- Tastenhinweise in der Seitenleiste (kurz! die Leiste ist schmal) ---
STR(STR_HINT_TAKE,   14,  "Take",               "Nehmen")
STR(STR_HINT_UNDO,   14,  "Undo",               "Zurück")
STR(STR_HINT_NEXT,   14,  "Next",               "Weiter")
STR(STR_HINT_CYCLE,  14,  "Cycle",              "Zyklus")
STR(STR_HINT_BACK,   14,  "Today",              "Heute")

// --- App-Glance im Starter ---
STR(STR_GLANCE_OPEN, 48,  "%d of %d still open", "%d von %d noch offen")
STR(STR_GLANCE_DONE, 48,  "All taken today",    "Heute alles genommen")
STR(STR_GLANCE_NONE, 48,  "No plan yet",        "Noch kein Plan")
