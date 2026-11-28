#pragma once

#include <stdint.h>

#include "pp_gachi_tuning.h"

// PokeGachi fire, water, grass battle. Pure logic: no Arduino, no allocation.
// Compiled into the sketch and also by the host tests in tests/gachi/.
// Shape of every mini game: begin, take inputs, read finished and won.

enum PpGachiPick : uint8_t { PP_PICK_FIRE, PP_PICK_WATER, PP_PICK_GRASS, PP_PICK_COUNT };
enum PpGachiRound : uint8_t { PP_ROUND_NONE, PP_ROUND_WIN, PP_ROUND_LOSE, PP_ROUND_TIE };

struct PpGachiBattle {
  bool active;
  uint8_t opponentDex;
  uint8_t myWins, theirWins;
  uint8_t cursor;                 // the pick under the cursor
  PpGachiPick myPick, theirPick;  // the last round thrown
  PpGachiRound lastRound;
  bool finished;
  bool won;
};

// Fire beats grass, grass beats water, water beats fire. From my side.
PpGachiRound ppGachiRoundOutcome(PpGachiPick mine, PpGachiPick theirs);
const char* ppGachiPickName(PpGachiPick pick);                           // "FIRE", "WATER", "GRASS"
void ppGachiBattleBegin(PpGachiBattle& b, uint8_t opponentDex);
void ppGachiBattleNext(PpGachiBattle& b);                                // cursor to the next pick, wraps
void ppGachiBattleThrow(PpGachiBattle& b, PpGachiPick theirPick);        // throws the cursor's pick
