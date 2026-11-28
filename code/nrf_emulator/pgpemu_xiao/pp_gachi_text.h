#pragma once

#include <stdint.h>

#include "pp_gachi_logic.h"

// PokeGachi result card texts. Pure: no Arduino, no allocation. Compiled into
// the sketch and also by the host tests in tests/gachi/.

enum PpGachiAction : uint8_t { PP_GACHI_ACT_FEED, PP_GACHI_ACT_PLAY, PP_GACHI_ACT_BATTLE, PP_GACHI_ACT_ADVANCE };

// Result text for a card: two short lines of 7-bit ASCII, at most 18 characters
// each. Never nullptr; an unused line is "".
void ppGachiResultText(PpGachiAction action, PpGachiResult result, const char*& line1, const char*& line2);
