#pragma once

#include "pp_board.h"
#include "pp_anim_logic.h"

#if PP_HAS_DISPLAY

#include <Adafruit_ST7789.h>

// Animation area: full width, from under the header row to the bottom edge.
static constexpr int16_t kAnimW = 240;
static constexpr int16_t kAnimH = 200;
static constexpr int16_t kAnimX = 0;
static constexpr int16_t kAnimY = 40;

// Draws one frame of `kind` at `progress` (0..1000) off-screen and pushes it to
// the panel. Returns false if the frame buffer could not be allocated.
bool ppAnimDrawFrame(Adafruit_ST7789& tft, PpAnimKind kind, uint16_t progress);

#endif
