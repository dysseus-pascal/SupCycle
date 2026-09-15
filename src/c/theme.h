#pragma once
#include <pebble.h>

// Farbschema petrol/weiss im Stil der Pebble-Timeline, wie in den
// Schwesterapps: weisser Grund, schwarze Schrift, dunkle Seitenleiste rechts.
//
// Warum Petrol: Drinktervall ist blau, Flynformer orange, Herzintervall
// violett, ChronoKit gruen. Eine fuenfte, deutlich andere Familie haelt sie im
// Starter auseinander.
//
// Der Schirm der Uhr LEUCHTET NICHT, er spiegelt. Helle Flaechen lesen sich
// draussen besser - deshalb Weiss als Grund und die Farbe nur in Leiste und
// Balken.
#define KI_COLOR_BG          GColorWhite
#define KI_COLOR_TEXT        GColorBlack

#define KI_SIDEBAR_W         PBL_IF_ROUND_ELSE(51, (PBL_DISPLAY_WIDTH >= 180 ? 34 : 30))
#define KI_COLOR_SIDEBAR     PBL_IF_COLOR_ELSE(GColorMidnightGreen, GColorBlack)
#define KI_COLOR_ON_SIDEBAR  GColorWhite

#define KI_COLOR_BAR         PBL_IF_COLOR_ELSE(GColorTiffanyBlue, GColorBlack)
#define KI_COLOR_BAR_EMPTY   GColorLightGray

#define KI_COLOR_BIG         PBL_IF_COLOR_ELSE(GColorMidnightGreen, GColorBlack)
#define KI_COLOR_DIM         PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack)
#define KI_COLOR_DONE        PBL_IF_COLOR_ELSE(GColorTiffanyBlue, GColorLightGray)

#define KI_MARGIN            PBL_IF_ROUND_ELSE(38, 9)
