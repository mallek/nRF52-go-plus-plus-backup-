// Host tests for pp_gachi_battle. Build + run from this folder (WSL or any g++):
//   g++ -std=c++17 -Wall -Wextra -Werror -I../../code/nrf_emulator/pgpemu_xiao
//       gachi_battle_test.cpp ../../code/nrf_emulator/pgpemu_xiao/pp_gachi_battle.cpp -o t
//   ./t
// Or all host tests: bash tests/run.sh

#include <cstdio>
#include <cstring>

#include "pp_gachi_battle.h"
#include "pp_gachi_tuning.h"

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

// Move the cursor to a pick, then throw it against theirs.
static void play(PpGachiBattle& b, PpGachiPick mine, PpGachiPick theirs)
{
  for (int i = 0; i < PP_PICK_COUNT && b.cursor != (uint8_t)mine; ++i) {
    ppGachiBattleNext(b);
  }
  ppGachiBattleThrow(b, theirs);
}

static void testAllNinePairs()
{
  const PpGachiRound want[3][3] = {
    // theirs:       FIRE            WATER           GRASS
    /* mine FIRE  */ {PP_ROUND_TIE, PP_ROUND_LOSE, PP_ROUND_WIN},
    /* mine WATER */ {PP_ROUND_WIN, PP_ROUND_TIE, PP_ROUND_LOSE},
    /* mine GRASS */ {PP_ROUND_LOSE, PP_ROUND_WIN, PP_ROUND_TIE},
  };
  for (int m = 0; m < 3; ++m) {
    for (int t = 0; t < 3; ++t) {
      CHECK(ppGachiRoundOutcome((PpGachiPick)m, (PpGachiPick)t) == want[m][t]);
    }
  }
  CHECK(std::strcmp(ppGachiPickName(PP_PICK_FIRE), "FIRE") == 0);
  CHECK(std::strcmp(ppGachiPickName(PP_PICK_WATER), "WATER") == 0);
  CHECK(std::strcmp(ppGachiPickName(PP_PICK_GRASS), "GRASS") == 0);
}

static void testCursorWraps()
{
  PpGachiBattle b = {};
  ppGachiBattleBegin(b, 25);
  CHECK(b.active);
  CHECK(b.opponentDex == 25);
  CHECK(b.cursor == PP_PICK_FIRE);
  ppGachiBattleNext(b);
  CHECK(b.cursor == PP_PICK_WATER);
  ppGachiBattleNext(b);
  CHECK(b.cursor == PP_PICK_GRASS);
  ppGachiBattleNext(b);
  CHECK(b.cursor == PP_PICK_FIRE);
}

static void testWinInTwo()
{
  PpGachiBattle b = {};
  ppGachiBattleBegin(b, 4);
  play(b, PP_PICK_FIRE, PP_PICK_GRASS);
  CHECK(b.lastRound == PP_ROUND_WIN);
  CHECK(b.myPick == PP_PICK_FIRE);
  CHECK(b.theirPick == PP_PICK_GRASS);
  CHECK(!b.finished);
  play(b, PP_PICK_WATER, PP_PICK_FIRE);
  CHECK(b.finished);
  CHECK(b.won);
  CHECK(b.myWins == PP_GACHI_ROUND_WINS);
  CHECK(b.theirWins == 0);
}

static void testLoseInTwo()
{
  PpGachiBattle b = {};
  ppGachiBattleBegin(b, 4);
  play(b, PP_PICK_FIRE, PP_PICK_WATER);
  play(b, PP_PICK_GRASS, PP_PICK_FIRE);
  CHECK(b.finished);
  CHECK(!b.won);
  CHECK(b.theirWins == PP_GACHI_ROUND_WINS);
  CHECK(b.myWins == 0);
}

static void testTiesReplay()
{
  PpGachiBattle b = {};
  ppGachiBattleBegin(b, 7);
  for (int i = 0; i < 5; ++i) {
    play(b, PP_PICK_WATER, PP_PICK_WATER);
    CHECK(b.lastRound == PP_ROUND_TIE);
    CHECK(!b.finished);
    CHECK(b.myWins == 0);
    CHECK(b.theirWins == 0);
  }
}

static void testGoesToThree()
{
  PpGachiBattle b = {};
  ppGachiBattleBegin(b, 7);
  play(b, PP_PICK_FIRE, PP_PICK_GRASS);   // win
  play(b, PP_PICK_FIRE, PP_PICK_WATER);   // lose
  CHECK(!b.finished);
  play(b, PP_PICK_GRASS, PP_PICK_WATER);  // win
  CHECK(b.finished);
  CHECK(b.won);
  CHECK(b.myWins == 2);
  CHECK(b.theirWins == 1);
}

static void testIgnoredAfterFinish()
{
  PpGachiBattle b = {};
  ppGachiBattleBegin(b, 7);
  play(b, PP_PICK_FIRE, PP_PICK_GRASS);
  play(b, PP_PICK_FIRE, PP_PICK_GRASS);
  CHECK(b.finished);
  PpGachiBattle before = b;
  ppGachiBattleNext(b);
  ppGachiBattleThrow(b, PP_PICK_WATER);
  CHECK(std::memcmp(&before, &b, sizeof(b)) == 0);
}

static void testInactiveIgnored()
{
  PpGachiBattle b = {};
  ppGachiBattleNext(b);
  ppGachiBattleThrow(b, PP_PICK_WATER);
  PpGachiBattle zero = {};
  CHECK(std::memcmp(&zero, &b, sizeof(b)) == 0);
}

static void testBadTheirPick()
{
  PpGachiBattle b = {};
  ppGachiBattleBegin(b, 7);
  ppGachiBattleThrow(b, (PpGachiPick)9);  // cursor FIRE vs FIRE
  CHECK(b.theirPick == PP_PICK_FIRE);
  CHECK(b.lastRound == PP_ROUND_TIE);
  ppGachiBattleNext(b);                   // WATER beats FIRE
  ppGachiBattleThrow(b, (PpGachiPick)9);
  CHECK(b.lastRound == PP_ROUND_WIN);
  CHECK(b.myWins == 1);
}

int main()
{
  testAllNinePairs();
  testCursorWraps();
  testWinInTwo();
  testLoseInTwo();
  testTiesReplay();
  testGoesToThree();
  testIgnoredAfterFinish();
  testInactiveIgnored();
  testBadTheirPick();
  if (failures == 0) {
    std::printf("gachi_battle: all passed\n");
    return 0;
  }
  std::printf("gachi_battle: %d failure(s)\n", failures);
  return 1;
}
