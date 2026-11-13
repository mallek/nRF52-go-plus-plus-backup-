#pragma once

#include "pp_board.h"
#include "pp_anim_logic.h"

#if PP_HAS_DISPLAY

#include <Adafruit_ST7789.h>

// Size of the animation square, centered on the 240x240 panel.
static constexpr int16_t kAnimSize = 160;
static constexpr int16_t kAnimOrigin = (240 - kAnimSize) / 2;

// Draws one frame of `kind` at `progress` (0..1000) off-screen and pushes it to
// the panel. Returns false if the frame buffer could not be allocated.
bool ppAnimDrawFrame(Adafruit_ST7789& tft, PpAnimKind kind, uint16_t progress);

#endif
