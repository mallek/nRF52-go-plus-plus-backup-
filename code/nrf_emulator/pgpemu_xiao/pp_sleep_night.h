#pragma once

#include <stdint.h>

// Pure per-night sleep-log generator: no Arduino, no hardware. Compiled into the sketch
// and also by the host tests in tests/sleep/.
//
// Shape follows the real ULEP records decoded from the unit dumps (see
// workspaces/pgpp-clone/2026-10-07-autocatcher-build-log.md): an entry is a 16-bit
// value, kind in the top 4 bits and a minute offset from the session start in the low 12.
// A night is a (4,0) start marker, an onset blip pair, 0-2 mid-night blip pairs, and the
// closing (1,L-2) (4,L-1|L) (4,L).

static constexpr uint8_t PP_SLEEP_NIGHT_MAX_ENTRIES = 16;
static constexpr uint16_t PP_SLEEP_NIGHT_MIN_LENGTH = 370;
static constexpr uint16_t PP_SLEEP_NIGHT_MAX_LENGTH = 520;
static constexpr uint16_t PP_SLEEP_NIGHT_MIN_SLACK = 3;
static constexpr uint16_t PP_SLEEP_NIGHT_MAX_SLACK = 40;

struct PpSleepNight {
  uint16_t lengthMin;  // minute of the closing marker, i.e. session length
  uint16_t slackMin;   // minutes between the session end and the sync (anchor)
  uint8_t count;       // number of valid entries
  uint16_t entries[PP_SLEEP_NIGHT_MAX_ENTRIES];
};

// Same seed gives the same night.
void ppSleepNightGenerate(uint32_t seed, PpSleepNight* out);

// Session start = anchor - (length + slack) minutes.
uint32_t ppSleepNightStart(const PpSleepNight& night, uint32_t anchor);
