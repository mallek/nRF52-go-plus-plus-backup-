#pragma once

#include "pp_board.h"
#include "pp_gachi_sprites.h"
#include "pp_gachi_stage.h"
#include "pp_menu_draw.h"

#if PP_HAS_DISPLAY

#include <Adafruit_GFX.h>

// Drawing for the pet stage: the scenes, the pet's frame and the drawn symbols.
// No state of its own and no allocation; everything is sized from the width and
// height passed in, so the same code serves a 240x240 and a 172x320 screen.
// Scenes, symbols and the empty ball are plain shapes drawn by this code.

// The stage on a w x h screen: full width, from under the top line down to ppGachiHomeLinesTop(h).
void ppGachiStageRect(int16_t w, int16_t h, int16_t& x, int16_t& y, int16_t& sw, int16_t& sh);

// Scene, pet (or empty ball) and symbol, drawn at the canvas origin in an sw x sh area.
// oldDex is the form drawn while pose.oldForm is set (0 when not evolving).
// WARNING: the area is the top-left corner of a canvas that may be wider than sw (the
// shared ppAnimCanvas() is 240 wide, so its row stride is 240 even when sw is 172).
// Never push it with one drawRGBBitmap(x, y, buffer, sw, sh): every row after the first
// would be skewed. Push it with ppGachiStagePush below.
void ppGachiStageDraw(Adafruit_GFX& canvas, int16_t sw, int16_t sh, const PpStagePose& pose,
                      uint8_t dex, bool shiny, uint8_t oldDex, const PpMenuColors& colors);

// Pushes the top-left sw x sh area of `canvas` to the screen at (x, y). One call when sw
// is the canvas width, else one call per row, each starting at its row in the canvas
// buffer. No heap. False (nothing drawn) when the canvas has no buffer or the area does
// not fit in it. A template so the panel's own fast drawRGBBitmap is used (Adafruit_SPITFT
// hides the slow Adafruit_GFX one rather than overriding it): pass the panel itself, for
// example the Adafruit_ST7789, not an Adafruit_GFX reference to it.
template <typename Screen>
bool ppGachiStagePush(Screen& screen, GFXcanvas16& canvas, int16_t x, int16_t y, int16_t sw, int16_t sh)
{
  uint16_t* buf = canvas.getBuffer();
  const int16_t stride = canvas.width();   // the canvas is never rotated
  if (buf == nullptr || sw <= 0 || sh <= 0 || sw > stride || sh > canvas.height()) {
    return false;
  }
  if (sw == stride) {
    screen.drawRGBBitmap(x, y, buf, sw, sh);
    return true;
  }
  for (int16_t row = 0; row < sh; ++row) {
    screen.drawRGBBitmap(x, static_cast<int16_t>(y + row), buf + static_cast<int32_t>(row) * stride, sw, 1);
  }
  return true;
}

// One frame of a sprite with its ground point at (cx, groundY), at a whole-number scale.
// Draws nothing for a null sprite or animation.
void ppGachiDrawFrame(Adafruit_GFX& gfx, int16_t cx, int16_t groundY, uint8_t scale,
                      const PpGachiSprite* sprite, const PpGachiAnim* anim, uint8_t frame, bool mirror, bool tipped);

// The whole-number scale that fits a species' Idle frame in a box. 0 when there is no
// Idle frame or it does not fit even at scale 1.
uint8_t ppGachiSpriteScale(const PpGachiSprite* sprite, int16_t boxW, int16_t boxH);

// The empty, open ball, its lower half resting on groundY, centred on cx, radius r.
void ppGachiDrawEmptyBall(Adafruit_GFX& gfx, int16_t cx, int16_t groundY, int16_t r);

// The dex-number placeholder box, centred in a box. With `fill` the inside is cleared to the background first.
void ppGachiDrawPlaceholder(Adafruit_GFX& gfx, int16_t x, int16_t y, int16_t boxW, int16_t boxH, uint8_t dex,
                            bool fill, const PpMenuColors& colors);

// A berry of radius r centred at (cx, cy).
void ppGachiDrawBerry(Adafruit_GFX& gfx, int16_t cx, int16_t cy, int16_t r);

#endif  // PP_HAS_DISPLAY
