#pragma once

#include <stdint.h>

#include "pp_feedback_logic.h"
#include "pp_gachi_tuning.h"

// PokeGachi game state, grants and pet arrival. Pure logic: no Arduino, no
// allocation. Compiled into the sketch and also by the host tests in tests/gachi/.

struct PpGachiRetired {
  uint8_t dex;
  bool shiny;
  uint16_t battlesWon;
};

struct PpGachiState {
  bool hasPet;
  uint8_t petDex;
  bool petShiny;
  uint8_t nutrition, happiness, experience;   // for the current stage
  uint16_t petBattlesWon;                     // this pet, all stages; saturates
  bool arrivalPending;                        // a new pet the player has not been shown
  uint8_t berries;
  uint8_t opponentCount;
  uint8_t opponents[PP_GACHI_OPPONENT_CAP];   // dex; [0] is the oldest
  uint16_t dexRaised;                         // bit = roster index
  uint16_t dexShiny;
  uint8_t sanctuaryCount;
  PpGachiRetired sanctuary[PP_GACHI_SANCTUARY_CAP];  // [0] is the oldest
  uint32_t rng;                               // xorshift32 state, never 0
};

enum PpGachiGrant : uint8_t {
  PP_GACHI_GRANT_NONE,
  PP_GACHI_GRANT_BERRY,
  PP_GACHI_GRANT_OPPONENT,
  PP_GACHI_GRANT_PET
};

void ppGachiInit(PpGachiState& st, uint32_t seed);        // a fresh game; seed 0 is replaced by a fixed non-zero value
uint32_t ppGachiRandom(PpGachiState& st, uint32_t n);     // 0..n-1; n == 0 returns 0
PpGachiGrant ppGachiOnEvent(PpGachiState& st, PpFeedbackEvent event, bool enabled);
bool ppGachiDexRaised(const PpGachiState& st, uint8_t rosterIndex);
bool ppGachiDexShiny(const PpGachiState& st, uint8_t rosterIndex);
uint8_t ppGachiDexRaisedCount(const PpGachiState& st);
bool ppGachiLineComplete(const PpGachiState& st, uint8_t line);  // its final form is raised

enum PpGachiResult : uint8_t {
  PP_GACHI_OK,
  PP_GACHI_NO_PET,
  PP_GACHI_NO_BERRIES,
  PP_GACHI_NUTRITION_FULL,
  PP_GACHI_PLAY_NEED_FOOD,     // happiness is held back by nutrition
  PP_GACHI_PLAY_NEED_BATTLE,   // held back by experience
  PP_GACHI_PLAY_NEED_BOTH,     // nutrition and experience are level and both below the need
  PP_GACHI_PLAY_FULL,          // happiness is full for this stage
  PP_GACHI_NO_OPPONENT,
  PP_GACHI_NOT_READY,
  PP_GACHI_EVOLVED,
  PP_GACHI_RETIRED
};

uint8_t ppGachiNeed(const PpGachiState& st);       // care points for the current stage; 0 with no pet
bool ppGachiReady(const PpGachiState& st);         // pet, and all three tracks >= need
bool ppGachiIsFinalForm(const PpGachiState& st);
PpGachiResult ppGachiFeed(PpGachiState& st);
PpGachiResult ppGachiPlay(PpGachiState& st);
PpGachiResult ppGachiBattleResult(PpGachiState& st, bool won);
PpGachiResult ppGachiAdvance(PpGachiState& st);    // evolve, or retire a ready final form
void ppGachiReset(PpGachiState& st);               // fresh game, keeps the random state
