#pragma once

#include <stdint.h>

// PokeGachi tuning values in one place. Pure constants, no Arduino.

static constexpr uint8_t PP_GACHI_BERRY_CAP = 10;
static constexpr uint8_t PP_GACHI_OPPONENT_CAP = 5;
static constexpr uint16_t PP_GACHI_SHINY_ODDS = 64;      // 1 in N
static constexpr uint8_t PP_GACHI_SANCTUARY_CAP = 32;
static constexpr uint8_t PP_GACHI_ROUND_WINS = 2;        // best two of three
static constexpr uint32_t PP_GACHI_CARD_MS = 2000;
static constexpr uint32_t PP_GACHI_ROUND_SHOW_MS = 1000;
static constexpr uint32_t PP_GACHI_GRANT_SAVE_MS = 30UL * 60UL * 1000UL;
