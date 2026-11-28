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
void ppGachiStageDraw(Adafruit_GFX& canvas, int16_t sw, int16_t sh, const PpStagePose& pose,
                      uint8_t dex, bool shiny, uint8_t oldDex, const PpMenuColors& colors);

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
