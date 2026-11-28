#pragma once

#include "pp_board.h"
#include "pp_gachi_battle.h"
#include "pp_gachi_flow.h"
#include "pp_gachi_logic.h"
#include "pp_gachi_text.h"
#include "pp_menu_draw.h"

#if PP_HAS_DISPLAY

#include <Adafruit_GFX.h>

// PokeGachi drawing: home face, result card and battle screen. No state, no
// buttons, no board #ifs: the layout comes from the width and height passed
// in, so the same code serves a 240x240 and a 172x320 screen. Text that does
// not fit is cut, never wrapped. All on-screen text is 7-bit ASCII.

struct PpGachiHomeLines {
  const char* topLeft;         // identity, for example "TRAVIS 1/3"
  const char* topRight;        // link word, for example "CONNECTED"
  uint16_t topRightColor;
  const char* pantryOverride;  // nullptr = draw the berries and opponents counts; else this text (the countdown)
  const char* bottom;          // counters, or the last event text
  uint16_t bottomColor;
};

// Whole home face: clears the screen, draws the top line, the sprite (or the "no pet yet" text), then the lines.
void ppGachiDrawHome(Adafruit_GFX& gfx, int16_t w, int16_t h, const PpGachiState& st, const PpGachiHomeLines& lines, const PpMenuColors& colors);
// Only the text area under the sprite (name, tracks or READY, pantry, bottom). Clears just that band.
void ppGachiDrawHomeLines(Adafruit_GFX& gfx, int16_t w, int16_t h, const PpGachiState& st, const PpGachiHomeLines& lines, const PpMenuColors& colors);
// Y of the band ppGachiDrawHomeLines owns, so the caller can draw the reset bar over it.
int16_t ppGachiHomeLinesTop(int16_t h);
void ppGachiDrawCard(Adafruit_GFX& gfx, int16_t w, int16_t h, const PpGachiCard& card, const PpMenuColors& colors);
void ppGachiDrawBattle(Adafruit_GFX& gfx, int16_t w, int16_t h, const PpGachiBattle& battle, bool roundShowing, uint8_t myDex, bool myShiny, const PpMenuColors& colors);
// Sprite centred in a box at the largest whole-number scale that fits; a placeholder when there is no sprite.
void ppGachiDrawSprite(Adafruit_GFX& gfx, int16_t x, int16_t y, int16_t boxW, int16_t boxH, uint8_t dex, bool shiny, const PpMenuColors& colors);

#endif  // PP_HAS_DISPLAY
