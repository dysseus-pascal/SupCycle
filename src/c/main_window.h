#pragma once
#include <pebble.h>

void main_window_push(void);

// Neu zeichnen. Für Änderungen, die nicht vom Schirm selbst kommen — etwa ein
// neuer Plan vom Telefon.
void main_window_refresh(void);
