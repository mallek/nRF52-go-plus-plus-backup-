// Host tests for pp_gachi_flow. Build + run from this folder (WSL or any g++):
//   g++ -std=c++17 -Wall -Wextra -Werror -I../../code/nrf_emulator/pgpemu_xiao
//       gachi_flow_test.cpp ../../code/nrf_emulator/pgpemu_xiao/pp_gachi_flow.cpp
//       ../../code/nrf_emulator/pgpemu_xiao/pp_gachi_battle.cpp -o t
//   ./t
// Or all host tests: bash tests/run.sh

#include <cstdio>
#include <cstring>

#include "pp_gachi_flow.h"

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

static PpGachiCard makeCard(bool arrival)
{
  PpGachiCard c = PpGachiCard();
  c.dex = 25;
  c.line1 = "PIKACHU";
  c.line2 = "HELLO";
  c.isArrival = arrival;
  return c;
}

static PpGachiFlowEvent upd(PpGachiFlow& f, bool n, bool s, PpGachiPick their, uint32_t now)
{
  return ppGachiFlowUpdate(f, n, s, their, now);
}

static void testIdleDoesNothing()
{
  PpGachiFlow f;
  ppGachiFlowReset(f);
  CHECK(f.screen == PP_GSCREEN_NONE);
  CHECK(upd(f, true, true, PP_PICK_FIRE, 5) == PP_GFLOW_NOTHING);
  CHECK(upd(f, false, false, PP_PICK_FIRE, 99999) == PP_GFLOW_NOTHING);
}

static void testCardTimesOut()
{
  PpGachiFlow f;
  ppGachiFlowReset(f);
  ppGachiFlowShowCard(f, makeCard(false), 1000);
  CHECK(f.screen == PP_GSCREEN_CARD);
  CHECK(upd(f, false, false, PP_PICK_FIRE, 2999) == PP_GFLOW_NOTHING);
  CHECK(f.screen == PP_GSCREEN_CARD);
  CHECK(upd(f, false, false, PP_PICK_FIRE, 3000) == PP_GFLOW_CARD_DONE);
  CHECK(f.screen == PP_GSCREEN_NONE);
  CHECK(upd(f, false, false, PP_PICK_FIRE, 3001) == PP_GFLOW_NOTHING);
}

static void testCardClosesOnEitherButton()
{
  PpGachiFlow f;
  ppGachiFlowReset(f);
  ppGachiFlowShowCard(f, makeCard(false), 1000);
  CHECK(upd(f, true, false, PP_PICK_FIRE, 1100) == PP_GFLOW_CARD_DONE);
  CHECK(f.screen == PP_GSCREEN_NONE);
  ppGachiFlowShowCard(f, makeCard(false), 1000);
  CHECK(upd(f, false, true, PP_PICK_FIRE, 1100) == PP_GFLOW_CARD_DONE);
  CHECK(f.screen == PP_GSCREEN_NONE);
}

static void testCardAcrossWrap()
{
  PpGachiFlow f;
  ppGachiFlowReset(f);
  uint32_t shown = 0xFFFFFC00u;
  ppGachiFlowShowCard(f, makeCard(false), shown);
  uint32_t due = shown + PP_GACHI_CARD_MS;  // wraps to 0x000003D0
  CHECK(due < shown);
  CHECK(upd(f, false, false, PP_PICK_FIRE, shown + 1) == PP_GFLOW_NOTHING);
  CHECK(upd(f, false, false, PP_PICK_FIRE, 0xFFFFFFFFu) == PP_GFLOW_NOTHING);
  CHECK(upd(f, false, false, PP_PICK_FIRE, due - 1) == PP_GFLOW_NOTHING);
  CHECK(upd(f, false, false, PP_PICK_FIRE, due) == PP_GFLOW_CARD_DONE);
}

static void testCardNullLines()
{
  PpGachiFlow f;
  ppGachiFlowReset(f);
  PpGachiCard c = PpGachiCard();
  c.line1 = nullptr;
  c.line2 = nullptr;
  ppGachiFlowShowCard(f, c, 0);
  CHECK(f.card.line1 != nullptr && std::strcmp(f.card.line1, "") == 0);
  CHECK(f.card.line2 != nullptr && std::strcmp(f.card.line2, "") == 0);
}

static void testCardKeepsArrivalFlag()
{
  PpGachiFlow f;
  ppGachiFlowReset(f);
  ppGachiFlowShowCard(f, makeCard(true), 0);
  CHECK(upd(f, true, false, PP_PICK_FIRE, 10) == PP_GFLOW_CARD_DONE);
  CHECK(f.card.isArrival);
  CHECK(f.card.dex == 25);
}

static void testCardAnimFieldsDefaultToZero()
{
  PpGachiCard c = PpGachiCard();
  CHECK(c.spriteAnim == 0);
  CHECK(!c.berry);
  PpGachiFlow f;
  ppGachiFlowReset(f);
  CHECK(f.card.spriteAnim == 0);
  CHECK(!f.card.berry);
}

static void testCardKeepsAnimFields()
{
  PpGachiFlow f;
  ppGachiFlowReset(f);
  PpGachiCard c = makeCard(false);
  c.spriteAnim = 3;   // Eat
  c.berry = true;
  ppGachiFlowShowCard(f, c, 0);
  CHECK(f.card.spriteAnim == 3);
  CHECK(f.card.berry);
  CHECK(upd(f, false, false, PP_PICK_FIRE, PP_GACHI_CARD_MS) == PP_GFLOW_CARD_DONE);
  CHECK(f.card.spriteAnim == 3);
  CHECK(f.card.berry);
}

static void testBattlePickAndThrow()
{
  PpGachiFlow f;
  ppGachiFlowReset(f);
  ppGachiFlowStartBattle(f, 4);
  CHECK(f.screen == PP_GSCREEN_BATTLE);
  CHECK(f.battle.opponentDex == 4);
  CHECK(upd(f, true, false, PP_PICK_GRASS, 100) == PP_GFLOW_REDRAW);
  CHECK(f.battle.cursor == PP_PICK_WATER);
  CHECK(upd(f, false, true, PP_PICK_GRASS, 200) == PP_GFLOW_REDRAW);
  CHECK(f.roundShowing);
  CHECK(f.roundUntil == 200 + PP_GACHI_ROUND_SHOW_MS);
  CHECK(f.battle.lastRound == PP_ROUND_LOSE);
}

static void testButtonsIgnoredWhileRoundShows()
{
  PpGachiFlow f;
  ppGachiFlowReset(f);
  ppGachiFlowStartBattle(f, 4);
  upd(f, true, false, PP_PICK_GRASS, 100);
  upd(f, false, true, PP_PICK_GRASS, 200);
  uint8_t cur = f.battle.cursor;
  CHECK(upd(f, false, true, PP_PICK_FIRE, 500) == PP_GFLOW_NOTHING);
  CHECK(upd(f, true, false, PP_PICK_FIRE, 600) == PP_GFLOW_NOTHING);
  CHECK(f.battle.myWins == 0);
  CHECK(f.battle.theirWins == 1);
  CHECK(f.battle.cursor == cur);
  CHECK(f.roundShowing);
}

static void testRoundEndsThenNext()
{
  PpGachiFlow f;
  ppGachiFlowReset(f);
  ppGachiFlowStartBattle(f, 4);
  upd(f, false, true, PP_PICK_WATER, 200);  // FIRE vs WATER: lose, unfinished
  CHECK(upd(f, false, false, PP_PICK_FIRE, 1199) == PP_GFLOW_NOTHING);
  CHECK(upd(f, false, false, PP_PICK_FIRE, 1200) == PP_GFLOW_REDRAW);
  CHECK(!f.roundShowing);
  CHECK(f.screen == PP_GSCREEN_BATTLE);
  CHECK(upd(f, true, false, PP_PICK_FIRE, 1300) == PP_GFLOW_REDRAW);
}

static void playWin(PpGachiFlow& f, uint32_t& t)
{
  // cursor starts at FIRE; FIRE beats GRASS.
  CHECK(upd(f, false, true, PP_PICK_GRASS, t) == PP_GFLOW_REDRAW);
  t += PP_GACHI_ROUND_SHOW_MS;
}

static void testBattleDoneWin()
{
  PpGachiFlow f;
  ppGachiFlowReset(f);
  ppGachiFlowStartBattle(f, 4);
  uint32_t t = 100;
  playWin(f, t);
  CHECK(upd(f, false, false, PP_PICK_FIRE, t) == PP_GFLOW_REDRAW);
  t += 10;
  playWin(f, t);
  CHECK(upd(f, false, false, PP_PICK_FIRE, t - 1) == PP_GFLOW_NOTHING);
  CHECK(upd(f, false, false, PP_PICK_FIRE, t) == PP_GFLOW_BATTLE_DONE);
  CHECK(f.battle.finished && f.battle.won);
  CHECK(f.screen == PP_GSCREEN_NONE);
  CHECK(upd(f, true, true, PP_PICK_FIRE, t + 1) == PP_GFLOW_NOTHING);
}

static void testBattleDoneLoss()
{
  PpGachiFlow f;
  ppGachiFlowReset(f);
  ppGachiFlowStartBattle(f, 4);
  uint32_t t = 0xFFFFFF00u;  // also crosses the wrap
  for (int i = 0; i < 2; ++i) {
    CHECK(upd(f, false, true, PP_PICK_WATER, t) == PP_GFLOW_REDRAW);  // FIRE vs WATER
    t += PP_GACHI_ROUND_SHOW_MS;
    PpGachiFlowEvent e = upd(f, false, false, PP_PICK_FIRE, t);
    CHECK(e == (i == 0 ? PP_GFLOW_REDRAW : PP_GFLOW_BATTLE_DONE));
    t += 10;
  }
  CHECK(f.battle.finished && !f.battle.won);
  CHECK(f.screen == PP_GSCREEN_NONE);
}

static void testBothEdgesIgnored()
{
  PpGachiFlow f;
  ppGachiFlowReset(f);
  ppGachiFlowStartBattle(f, 4);
  uint8_t cur = f.battle.cursor;
  CHECK(upd(f, true, true, PP_PICK_GRASS, 100) == PP_GFLOW_NOTHING);
  CHECK(f.battle.cursor == cur);
  CHECK(!f.roundShowing);
  CHECK(f.battle.myWins == 0 && f.battle.theirWins == 0);
}

static void testAbandon()
{
  PpGachiFlow f;
  ppGachiFlowReset(f);
  ppGachiFlowStartBattle(f, 4);
  upd(f, false, true, PP_PICK_GRASS, 100);
  ppGachiFlowAbandon(f);
  CHECK(f.screen == PP_GSCREEN_NONE);
  CHECK(upd(f, true, true, PP_PICK_FIRE, 5000) == PP_GFLOW_NOTHING);
  CHECK(upd(f, false, false, PP_PICK_FIRE, 9000) == PP_GFLOW_NOTHING);
}

static void testOneEventPerCall()
{
  PpGachiFlow f;
  ppGachiFlowReset(f);
  ppGachiFlowStartBattle(f, 4);
  uint32_t t = 100;
  playWin(f, t);
  upd(f, false, false, PP_PICK_FIRE, t);
  t += 10;
  playWin(f, t);
  // The finishing update carries both edges: they are consumed, not acted on.
  CHECK(upd(f, true, true, PP_PICK_FIRE, t) == PP_GFLOW_BATTLE_DONE);
  CHECK(f.screen == PP_GSCREEN_NONE);
  CHECK(f.battle.cursor == PP_PICK_FIRE);

  // A card closing does not touch a battle started afterwards by the caller.
  ppGachiFlowReset(f);
  ppGachiFlowShowCard(f, makeCard(true), 0);
  CHECK(upd(f, true, true, PP_PICK_FIRE, 10) == PP_GFLOW_CARD_DONE);
  CHECK(f.screen == PP_GSCREEN_NONE);
  CHECK(!f.battle.active);

  // A throw does not also end the round in the same call.
  ppGachiFlowStartBattle(f, 4);
  CHECK(upd(f, false, true, PP_PICK_GRASS, 50) == PP_GFLOW_REDRAW);
  CHECK(f.roundShowing);
}

int main()
{
  testIdleDoesNothing();
  testCardTimesOut();
  testCardClosesOnEitherButton();
  testCardAcrossWrap();
  testCardNullLines();
  testCardKeepsArrivalFlag();
  testCardAnimFieldsDefaultToZero();
  testCardKeepsAnimFields();
  testBattlePickAndThrow();
  testButtonsIgnoredWhileRoundShows();
  testRoundEndsThenNext();
  testBattleDoneWin();
  testBattleDoneLoss();
  testBothEdgesIgnored();
  testAbandon();
  testOneEventPerCall();
  if (failures == 0) {
    std::printf("gachi_flow: all passed\n");
    return 0;
  }
  std::printf("gachi_flow: %d failure(s)\n", failures);
  return 1;
}
