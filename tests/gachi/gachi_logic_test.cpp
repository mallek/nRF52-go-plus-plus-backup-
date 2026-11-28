// Host tests for pp_gachi_logic. Build + run from this folder (WSL or any g++):
//   g++ -std=c++17 -Wall -Wextra -Werror -I../../code/nrf_emulator/pgpemu_xiao
//       gachi_logic_test.cpp ../../code/nrf_emulator/pgpemu_xiao/pp_gachi_logic.cpp
//       ../../code/nrf_emulator/pgpemu_xiao/pp_gachi_species.cpp -o t
//   ./t
// Or all host tests: bash tests/run.sh

#include <cstdio>
#include <cstring>

#include "pp_gachi_logic.h"
#include "pp_gachi_species.h"
#include "pp_gachi_tuning.h"

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

static void markLineComplete(PpGachiState& st, uint8_t line)
{
  int8_t idx = ppGachiSpeciesIndex(ppGachiLineFinalDex(line));
  st.dexRaised = (uint16_t)(st.dexRaised | (1u << idx));
}

static void testInit()
{
  const uint32_t seeds[] = {0, 7};
  for (uint32_t seed : seeds) {
    PpGachiState st;
    std::memset(&st, 0xAB, sizeof(st));
    ppGachiInit(st, seed);
    CHECK(!st.hasPet);
    CHECK(!st.arrivalPending);
    CHECK(st.berries == 0);
    CHECK(st.opponentCount == 0);
    CHECK(st.dexRaised == 0);
    CHECK(st.dexShiny == 0);
    CHECK(st.sanctuaryCount == 0);
    CHECK(st.petBattlesWon == 0);
    CHECK(st.nutrition == 0 && st.happiness == 0 && st.experience == 0);
    CHECK(st.rng != 0);
  }
}

static void testRandomRange()
{
  PpGachiState st;
  ppGachiInit(st, 0);
  bool seen[11] = {};
  for (int i = 0; i < 10000; ++i) {
    uint32_t r = ppGachiRandom(st, 11);
    CHECK(r < 11);
    if (r < 11) {
      seen[r] = true;
    }
    CHECK(st.rng != 0);
  }
  for (int i = 0; i < 11; ++i) {
    CHECK(seen[i]);
  }
  CHECK(ppGachiRandom(st, 0) == 0);
  CHECK(st.rng != 0);
}

static void testSpinGrantsBerryToCap()
{
  PpGachiState st;
  ppGachiInit(st, 3);
  int berry = 0;
  int none = 0;
  for (int i = 0; i < 12; ++i) {
    PpGachiGrant g = ppGachiOnEvent(st, PP_FEEDBACK_POKESTOP_SPUN, true);
    if (g == PP_GACHI_GRANT_BERRY) {
      CHECK(none == 0);
      ++berry;
    } else if (g == PP_GACHI_GRANT_NONE) {
      ++none;
    }
  }
  CHECK(st.berries == PP_GACHI_BERRY_CAP);
  CHECK(berry == 10);
  CHECK(none == 2);
}

static void testDisabledIgnoresAll()
{
  PpGachiState st;
  ppGachiInit(st, 5);
  PpGachiState before;
  std::memcpy(&before, &st, sizeof(st));
  CHECK(ppGachiOnEvent(st, PP_FEEDBACK_POKESTOP_SPUN, false) == PP_GACHI_GRANT_NONE);
  CHECK(ppGachiOnEvent(st, PP_FEEDBACK_CAUGHT, false) == PP_GACHI_GRANT_NONE);
  CHECK(std::memcmp(&before, &st, sizeof(st)) == 0);
}

static void testFirstCatchDeliversStarter()
{
  for (uint32_t seed = 1; seed <= 300; ++seed) {
    PpGachiState st;
    ppGachiInit(st, seed);
    CHECK(ppGachiOnEvent(st, PP_FEEDBACK_CAUGHT, true) == PP_GACHI_GRANT_PET);
    CHECK(st.hasPet);
    CHECK(st.petDex == 1 || st.petDex == 4 || st.petDex == 7);
    CHECK(st.arrivalPending);
    CHECK(st.opponentCount == 0);
    int8_t idx = ppGachiSpeciesIndex(st.petDex);
    CHECK(idx >= 0);
    if (idx >= 0) {
      CHECK(ppGachiDexRaised(st, (uint8_t)idx));
      CHECK(ppGachiDexRaisedCount(st) == 1);
    }
    CHECK(st.nutrition == 0 && st.happiness == 0 && st.experience == 0);
    CHECK(st.petBattlesWon == 0);
  }
}

static void testCatchWithPetGrantsOpponentToCap()
{
  PpGachiState st;
  ppGachiInit(st, 11);
  CHECK(ppGachiOnEvent(st, PP_FEEDBACK_CAUGHT, true) == PP_GACHI_GRANT_PET);
  for (int i = 0; i < 5; ++i) {
    CHECK(ppGachiOnEvent(st, PP_FEEDBACK_CAUGHT, true) == PP_GACHI_GRANT_OPPONENT);
    CHECK(st.opponentCount == i + 1);
  }
  uint8_t kept[PP_GACHI_OPPONENT_CAP];
  std::memcpy(kept, st.opponents, sizeof(kept));
  for (int i = 0; i < 2; ++i) {
    CHECK(ppGachiOnEvent(st, PP_FEEDBACK_CAUGHT, true) == PP_GACHI_GRANT_NONE);
  }
  CHECK(st.opponentCount == PP_GACHI_OPPONENT_CAP);
  CHECK(std::memcmp(kept, st.opponents, sizeof(kept)) == 0);
  for (int i = 0; i < PP_GACHI_OPPONENT_CAP; ++i) {
    CHECK(ppGachiSpeciesIndex(st.opponents[i]) >= 0);
  }

  // order kept: replay with the same seed and compare the append order
  PpGachiState b;
  ppGachiInit(b, 11);
  ppGachiOnEvent(b, PP_FEEDBACK_CAUGHT, true);
  for (int i = 0; i < 5; ++i) {
    uint8_t before[PP_GACHI_OPPONENT_CAP];
    std::memcpy(before, b.opponents, sizeof(before));
    ppGachiOnEvent(b, PP_FEEDBACK_CAUGHT, true);
    for (int j = 0; j < i; ++j) {
      CHECK(b.opponents[j] == before[j]);
    }
    CHECK(b.opponents[i] == st.opponents[i]);
  }
}

static void testOffThenOnKeepsState()
{
  PpGachiState st;
  ppGachiInit(st, 21);
  ppGachiOnEvent(st, PP_FEEDBACK_POKESTOP_SPUN, true);
  ppGachiOnEvent(st, PP_FEEDBACK_POKESTOP_SPUN, true);
  ppGachiOnEvent(st, PP_FEEDBACK_CAUGHT, true);
  ppGachiOnEvent(st, PP_FEEDBACK_CAUGHT, true);
  CHECK(st.berries == 2);
  CHECK(st.opponentCount == 1);
  PpGachiState before;
  std::memcpy(&before, &st, sizeof(st));
  for (int i = 0; i < 20; ++i) {
    CHECK(ppGachiOnEvent(st, PP_FEEDBACK_POKESTOP_SPUN, false) == PP_GACHI_GRANT_NONE);
    CHECK(ppGachiOnEvent(st, PP_FEEDBACK_CAUGHT, false) == PP_GACHI_GRANT_NONE);
  }
  CHECK(std::memcmp(&before, &st, sizeof(st)) == 0);
  CHECK(ppGachiOnEvent(st, PP_FEEDBACK_POKESTOP_SPUN, true) == PP_GACHI_GRANT_BERRY);
  CHECK(st.berries == 3);
  CHECK(ppGachiOnEvent(st, PP_FEEDBACK_CAUGHT, true) == PP_GACHI_GRANT_OPPONENT);
  CHECK(st.opponentCount == 2);
}

static void testArrivalPrefersIncompleteLines()
{
  for (uint32_t seed = 1; seed <= 200; ++seed) {
    PpGachiState st;
    ppGachiInit(st, seed);
    markLineComplete(st, 0);
    markLineComplete(st, 1);
    markLineComplete(st, 2);
    CHECK(ppGachiLineComplete(st, 0));
    CHECK(!ppGachiLineComplete(st, 3));
    CHECK(ppGachiOnEvent(st, PP_FEEDBACK_CAUGHT, true) == PP_GACHI_GRANT_PET);
    CHECK(st.petDex == 25);
  }
}

static void testArrivalWhenAllComplete()
{
  bool seen[PP_GACHI_LINE_COUNT] = {};
  for (uint32_t seed = 1; seed <= 400; ++seed) {
    PpGachiState st;
    ppGachiInit(st, seed);
    for (uint8_t l = 0; l < PP_GACHI_LINE_COUNT; ++l) {
      markLineComplete(st, l);
    }
    CHECK(ppGachiOnEvent(st, PP_FEEDBACK_CAUGHT, true) == PP_GACHI_GRANT_PET);
    for (uint8_t l = 0; l < PP_GACHI_LINE_COUNT; ++l) {
      if (st.petDex == ppGachiLineBaseDex(l)) {
        seen[l] = true;
      }
    }
  }
  for (uint8_t l = 0; l < PP_GACHI_LINE_COUNT; ++l) {
    CHECK(seen[l]);
  }
}

static void testShinyOdds()
{
  PpGachiState st;
  ppGachiInit(st, 99);
  int shiny = 0;
  for (int i = 0; i < 6400; ++i) {
    st.hasPet = false;
    st.petShiny = false;
    CHECK(ppGachiOnEvent(st, PP_FEEDBACK_CAUGHT, true) == PP_GACHI_GRANT_PET);
    int8_t idx = ppGachiSpeciesIndex(st.petDex);
    if (st.petShiny) {
      ++shiny;
      CHECK(idx >= 0 && ppGachiDexShiny(st, (uint8_t)idx));
    }
  }
  CHECK(shiny >= 50 && shiny <= 160);
  CHECK(st.dexShiny != 0);
}

static void testCatchDeliveringPetKeepsPantry()
{
  PpGachiState st;
  ppGachiInit(st, 13);
  st.berries = PP_GACHI_BERRY_CAP;
  st.opponentCount = PP_GACHI_OPPONENT_CAP;
  for (int i = 0; i < PP_GACHI_OPPONENT_CAP; ++i) {
    st.opponents[i] = (uint8_t)(i + 1);
  }
  CHECK(ppGachiOnEvent(st, PP_FEEDBACK_CAUGHT, true) == PP_GACHI_GRANT_PET);
  CHECK(st.berries == PP_GACHI_BERRY_CAP);
  CHECK(st.opponentCount == PP_GACHI_OPPONENT_CAP);
  for (int i = 0; i < PP_GACHI_OPPONENT_CAP; ++i) {
    CHECK(st.opponents[i] == i + 1);
  }

  // and below the caps, with no pet
  PpGachiState b;
  ppGachiInit(b, 14);
  b.berries = 3;
  b.opponentCount = 2;
  b.opponents[0] = 4;
  b.opponents[1] = 7;
  CHECK(ppGachiOnEvent(b, PP_FEEDBACK_CAUGHT, true) == PP_GACHI_GRANT_PET);
  CHECK(b.berries == 3);
  CHECK(b.opponentCount == 2);
  CHECK(b.opponents[0] == 4 && b.opponents[1] == 7);
}

static void testOtherEventsIgnored()
{
  const PpFeedbackEvent others[] = {PP_FEEDBACK_FLED, PP_FEEDBACK_CONNECTED, PP_FEEDBACK_READY,
                                    PP_FEEDBACK_BAG_FULL};
  PpGachiState st;
  ppGachiInit(st, 17);
  ppGachiOnEvent(st, PP_FEEDBACK_CAUGHT, true);
  PpGachiState before;
  std::memcpy(&before, &st, sizeof(st));
  for (PpFeedbackEvent e : others) {
    CHECK(ppGachiOnEvent(st, e, true) == PP_GACHI_GRANT_NONE);
  }
  CHECK(std::memcmp(&before, &st, sizeof(st)) == 0);
}

int main()
{
  testInit();
  testRandomRange();
  testSpinGrantsBerryToCap();
  testDisabledIgnoresAll();
  testFirstCatchDeliversStarter();
  testCatchWithPetGrantsOpponentToCap();
  testOffThenOnKeepsState();
  testArrivalPrefersIncompleteLines();
  testArrivalWhenAllComplete();
  testShinyOdds();
  testCatchDeliveringPetKeepsPantry();
  testOtherEventsIgnored();
  if (failures == 0) {
    std::printf("gachi_logic: all passed\n");
    return 0;
  }
  std::printf("gachi_logic: %d failure(s)\n", failures);
  return 1;
}
