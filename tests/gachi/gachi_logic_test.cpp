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

// A state holding a pet of the given dex with the given tracks.
static PpGachiState petState(uint8_t dex, uint8_t n, uint8_t h, uint8_t x)
{
  PpGachiState st;
  ppGachiInit(st, 4242);
  st.hasPet = true;
  st.petDex = dex;
  st.nutrition = n;
  st.happiness = h;
  st.experience = x;
  int8_t idx = ppGachiSpeciesIndex(dex);
  st.dexRaised = (uint16_t)(1u << idx);
  return st;
}

static void testNeedAndReady()
{
  PpGachiState none;
  ppGachiInit(none, 1);
  CHECK(ppGachiNeed(none) == 0);
  CHECK(!ppGachiReady(none));
  PpGachiState a = petState(1, 5, 5, 4);
  CHECK(ppGachiNeed(a) == 5);
  CHECK(!ppGachiReady(a));
  a.experience = 5;
  CHECK(ppGachiReady(a));
  PpGachiState b = petState(2, 5, 5, 5);
  CHECK(ppGachiNeed(b) == 10);
  CHECK(!ppGachiReady(b));
  CHECK(ppGachiNeed(petState(25, 0, 0, 0)) == 8);
  CHECK(ppGachiNeed(petState(26, 0, 0, 0)) == 5);
  CHECK(!ppGachiIsFinalForm(a));
  CHECK(ppGachiIsFinalForm(petState(3, 0, 0, 0)));
  CHECK(ppGachiIsFinalForm(petState(26, 0, 0, 0)));
  CHECK(!ppGachiIsFinalForm(none));
}

static void testFeed()
{
  PpGachiState none;
  ppGachiInit(none, 1);
  none.berries = 3;
  CHECK(ppGachiFeed(none) == PP_GACHI_NO_PET);
  CHECK(none.berries == 3);

  PpGachiState st = petState(1, 0, 0, 0);
  PpGachiState before;
  std::memcpy(&before, &st, sizeof(st));
  CHECK(ppGachiFeed(st) == PP_GACHI_NO_BERRIES);
  CHECK(std::memcmp(&before, &st, sizeof(st)) == 0);

  st.berries = 4;
  CHECK(ppGachiFeed(st) == PP_GACHI_OK);
  CHECK(st.berries == 3);
  CHECK(st.nutrition == 1);

  st.nutrition = 5;
  CHECK(ppGachiFeed(st) == PP_GACHI_NUTRITION_FULL);
  CHECK(st.berries == 3);
  CHECK(st.nutrition == 5);
  st.berries = 0;
  CHECK(ppGachiFeed(st) == PP_GACHI_NUTRITION_FULL);
}

static void testPlayLimit()
{
  PpGachiState a = petState(2, 0, 0, 0);
  CHECK(ppGachiPlay(a) == PP_GACHI_PLAY_NEED_BOTH);
  CHECK(a.happiness == 0);
  PpGachiState b = petState(2, 2, 0, 0);
  CHECK(ppGachiPlay(b) == PP_GACHI_PLAY_NEED_BATTLE);
  CHECK(b.happiness == 0);
  PpGachiState c = petState(2, 0, 0, 2);
  CHECK(ppGachiPlay(c) == PP_GACHI_PLAY_NEED_FOOD);
  CHECK(c.happiness == 0);
  PpGachiState d = petState(2, 2, 0, 3);
  CHECK(ppGachiPlay(d) == PP_GACHI_OK);
  CHECK(ppGachiPlay(d) == PP_GACHI_OK);
  CHECK(d.happiness == 2);
  CHECK(ppGachiPlay(d) == PP_GACHI_PLAY_NEED_FOOD);
  CHECK(d.happiness == 2);
  PpGachiState none;
  ppGachiInit(none, 1);
  CHECK(ppGachiPlay(none) == PP_GACHI_NO_PET);
  for (uint8_t n = 0; n <= 10; ++n) {
    for (uint8_t x = 0; x <= 10; ++x) {
      PpGachiState s = petState(2, n, 0, x);
      for (int i = 0; i < 20; ++i) {
        ppGachiPlay(s);
        CHECK(s.happiness <= (n < x ? n : x));
      }
    }
  }
}

static void testPlayFull()
{
  PpGachiState st = petState(1, 5, 5, 5);
  CHECK(ppGachiPlay(st) == PP_GACHI_PLAY_FULL);
  CHECK(st.happiness == 5);
}

static void testBattleWin()
{
  PpGachiState st = petState(1, 0, 0, 0);
  st.opponentCount = 3;
  st.opponents[0] = 4;
  st.opponents[1] = 7;
  st.opponents[2] = 25;
  CHECK(ppGachiBattleResult(st, true) == PP_GACHI_OK);
  CHECK(st.opponentCount == 2);
  CHECK(st.opponents[0] == 7 && st.opponents[1] == 25);
  CHECK(st.experience == 1);
  CHECK(st.petBattlesWon == 1);
}

static void testBattleWinAtFullExperience()
{
  PpGachiState st = petState(1, 0, 0, 5);
  st.opponentCount = 2;
  st.opponents[0] = 4;
  st.opponents[1] = 7;
  st.petBattlesWon = 65535;
  CHECK(ppGachiBattleResult(st, true) == PP_GACHI_OK);
  CHECK(st.opponentCount == 1 && st.opponents[0] == 7);
  CHECK(st.experience == 5);
  CHECK(st.petBattlesWon == 65535);
  st.petBattlesWon = 9;
  st.opponentCount = 1;
  CHECK(ppGachiBattleResult(st, true) == PP_GACHI_OK);
  CHECK(st.petBattlesWon == 10);
  CHECK(st.opponentCount == 0);
}

static void testBattleLossKeepsOpponent()
{
  PpGachiState st = petState(1, 2, 1, 1);
  st.opponentCount = 2;
  st.opponents[0] = 4;
  st.opponents[1] = 7;
  PpGachiState before;
  std::memcpy(&before, &st, sizeof(st));
  CHECK(ppGachiBattleResult(st, false) == PP_GACHI_OK);
  CHECK(std::memcmp(&before, &st, sizeof(st)) == 0);
}

static void testBattleNoOpponent()
{
  PpGachiState st = petState(1, 0, 0, 0);
  CHECK(ppGachiBattleResult(st, true) == PP_GACHI_NO_OPPONENT);
  CHECK(ppGachiBattleResult(st, false) == PP_GACHI_NO_OPPONENT);
  PpGachiState none;
  ppGachiInit(none, 1);
  none.opponentCount = 1;
  none.opponents[0] = 4;
  CHECK(ppGachiBattleResult(none, true) == PP_GACHI_NO_PET);
  CHECK(none.opponentCount == 1);
}

static void testEvolve()
{
  PpGachiState st = petState(1, 5, 5, 5);
  st.petShiny = true;
  st.petBattlesWon = 12;
  CHECK(ppGachiAdvance(st) == PP_GACHI_EVOLVED);
  CHECK(st.hasPet);
  CHECK(st.petDex == 2);
  CHECK(st.nutrition == 0 && st.happiness == 0 && st.experience == 0);
  CHECK(ppGachiDexRaised(st, (uint8_t)ppGachiSpeciesIndex(2)));
  CHECK(ppGachiDexShiny(st, (uint8_t)ppGachiSpeciesIndex(2)));
  CHECK(ppGachiNeed(st) == 10);
  CHECK(st.petShiny);
  CHECK(st.petBattlesWon == 12);

  PpGachiState plain = petState(1, 5, 5, 5);
  CHECK(ppGachiAdvance(plain) == PP_GACHI_EVOLVED);
  CHECK(!ppGachiDexShiny(plain, (uint8_t)ppGachiSpeciesIndex(2)));
}

static void testRetire()
{
  PpGachiState st = petState(3, 10, 10, 10);
  st.petShiny = true;
  st.petBattlesWon = 77;
  st.dexRaised = 0;
  CHECK(!ppGachiLineComplete(st, 0));
  markLineComplete(st, 0);
  CHECK(ppGachiAdvance(st) == PP_GACHI_RETIRED);
  CHECK(!st.hasPet);
  CHECK(st.nutrition == 0 && st.happiness == 0 && st.experience == 0);
  CHECK(st.sanctuaryCount == 1);
  CHECK(st.sanctuary[0].dex == 3);
  CHECK(st.sanctuary[0].shiny);
  CHECK(st.sanctuary[0].battlesWon == 77);
  CHECK(ppGachiLineComplete(st, 0));
}

static void testAdvanceNotReady()
{
  PpGachiState st = petState(1, 5, 5, 4);
  PpGachiState before;
  std::memcpy(&before, &st, sizeof(st));
  CHECK(ppGachiAdvance(st) == PP_GACHI_NOT_READY);
  CHECK(std::memcmp(&before, &st, sizeof(st)) == 0);
  PpGachiState none;
  ppGachiInit(none, 1);
  CHECK(ppGachiAdvance(none) == PP_GACHI_NO_PET);
}

static void testSanctuaryKeepsNewest32()
{
  PpGachiState st;
  ppGachiInit(st, 8);
  for (int i = 0; i <= 32; ++i) {
    st.hasPet = true;
    st.petDex = 3;
    st.petShiny = false;
    st.nutrition = st.happiness = st.experience = 10;
    st.petBattlesWon = (uint16_t)i;
    CHECK(ppGachiAdvance(st) == PP_GACHI_RETIRED);
  }
  CHECK(st.sanctuaryCount == PP_GACHI_SANCTUARY_CAP);
  CHECK(st.sanctuary[0].battlesWon == 1);
  CHECK(st.sanctuary[31].battlesWon == 32);
}

static void testReset()
{
  PpGachiState st;
  ppGachiInit(st, 31);
  ppGachiOnEvent(st, PP_FEEDBACK_POKESTOP_SPUN, true);
  ppGachiOnEvent(st, PP_FEEDBACK_CAUGHT, true);
  ppGachiFeed(st);
  ppGachiPlay(st);
  uint32_t rng = st.rng;
  ppGachiReset(st);
  CHECK(st.rng == rng && st.rng != 0);
  PpGachiState fresh;
  ppGachiInit(fresh, 1);
  fresh.rng = rng;
  CHECK(std::memcmp(&fresh, &st, sizeof(st)) == 0);
}

static void checkInvariants(const PpGachiState& st)
{
  CHECK(st.berries <= PP_GACHI_BERRY_CAP);
  CHECK(st.opponentCount <= PP_GACHI_OPPONENT_CAP);
  uint8_t need = ppGachiNeed(st);
  CHECK(st.nutrition <= need);
  CHECK(st.happiness <= need);
  CHECK(st.experience <= need);
  uint8_t m = st.nutrition < st.experience ? st.nutrition : st.experience;
  CHECK(st.happiness <= m);
  CHECK(st.sanctuaryCount <= PP_GACHI_SANCTUARY_CAP);
  if (st.hasPet) {
    CHECK(ppGachiSpeciesIndex(st.petDex) >= 0);
  }
}

static bool allLinesComplete(const PpGachiState& st)
{
  for (uint8_t l = 0; l < PP_GACHI_LINE_COUNT; ++l) {
    if (!ppGachiLineComplete(st, l)) {
      return false;
    }
  }
  return true;
}

static void testLongRun()
{
  PpGachiState st;
  ppGachiInit(st, 12345);
  int steps = 0;
  bool done = false;
  while (steps < 200000 && !done) {
    ++steps;
    ppGachiOnEvent(st, PP_FEEDBACK_POKESTOP_SPUN, true);
    ppGachiOnEvent(st, PP_FEEDBACK_CAUGHT, true);
    while (ppGachiFeed(st) == PP_GACHI_OK) {
    }
    while (st.hasPet && st.opponentCount > 0 && st.experience < ppGachiNeed(st)) {
      ppGachiBattleResult(st, true);
    }
    while (ppGachiPlay(st) == PP_GACHI_OK) {
    }
    ppGachiAdvance(st);
    checkInvariants(st);
    done = allLinesComplete(st);
  }
  CHECK(done);
  CHECK(ppGachiDexRaisedCount(st) == 11);
  std::printf("gachi_logic: long run finished in %d steps\n", steps);
}

int main()
{
  testNeedAndReady();
  testFeed();
  testPlayLimit();
  testPlayFull();
  testBattleWin();
  testBattleWinAtFullExperience();
  testBattleLossKeepsOpponent();
  testBattleNoOpponent();
  testEvolve();
  testRetire();
  testAdvanceNotReady();
  testSanctuaryKeepsNewest32();
  testReset();
  testLongRun();
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
