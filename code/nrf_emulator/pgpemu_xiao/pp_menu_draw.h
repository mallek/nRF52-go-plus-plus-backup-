#pragma once

#include "pp_board.h"
#include "pp_menu_logic.h"

#if PP_HAS_DISPLAY

#include <Adafruit_GFX.h>

// Draws a menu view. No state, no buttons, no board #ifs: the layout comes from
// the width and height passed in, so the same code serves a 240x240 and a
// 172x320 screen.

struct PpMenuColors {
  uint16_t bg;
  uint16_t fg;
  uint16_t dim;
  uint16_t accent;
};

// How many rows fit on a screen of height h (title and hint line excluded).
// Pass this as maxRows to ppMenuBuildView.
uint8_t ppMenuRowsThatFit(int16_t h);

// Clears the screen and draws the whole view.
void ppMenuDraw(Adafruit_GFX& gfx, int16_t w, int16_t h, const PpMenuView& view, const PpMenuColors& colors);

#endif  // PP_HAS_DISPLAY
