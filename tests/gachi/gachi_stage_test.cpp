// Host tests for pp_gachi_stage. Build + run from this folder (WSL or any g++):
//   g++ -std=c++17 -Wall -Wextra -Werror -I../../code/nrf_emulator/pgpemu_xiao
//       gachi_stage_test.cpp ../../code/nrf_emulator/pgpemu_xiao/pp_gachi_stage.cpp -o t
//   ./t
// Or all host tests: bash tests/run.sh

#include <cstdio>

#include "pp_gachi_stage.h"

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

static const uint16_t kIdleMs[4] = {200, 100, 100, 100};
static const uint16_t kHopMs[10] = {50, 50, 50, 50, 50, 50, 50, 50, 50, 50};
static const uint16_t kSleepMs[2] = {500, 600};
static const uint16_t kEatMs[4] = {100, 100, 100, 100};
static const uint16_t kWalkMs[4] = {100, 100, 100, 100};

static PpStageTimings fixture()
{
  PpStageTimings t = {};
  t.anim[PP_SANIM_IDLE] = {4, kIdleMs};
  t.anim[PP_SANIM_HOP] = {10, kHopMs};
  t.anim[PP_SANIM_SLEEP] = {2, kSleepMs};
  t.anim[PP_SANIM_EAT] = {4, kEatMs};
  t.anim[PP_SANIM_WALK] = {4, kWalkMs};
  return t;
}

static const PpStageTimings T = fixture();

static PpStagePose at(PpStage& st, uint32_t now)
{
  PpStagePose p = {};
  ppStagePose(st, T, now, p);
  return p;
}

static void idleStage(PpStage& st, uint32_t t0)
{
  ppStageInit(st, t0);
  ppStageSetBase(st, PP_STAGE_IDLE, t0);
}

static void testInitIsEmpty()
{
  PpStage st;
  ppStageInit(st, 1000);
  PpStagePose p = {};
  CHECK(ppStagePose(st, T, 1000, p));
  CHECK(p.emptyBall);
  CHECK(p.scene == PP_SCENE_MEADOW);
  CHECK(p.symbol == PP_SYM_NONE);
  CHECK(!ppStagePose(st, T, 1010, p));
  CHECK(!ppStageBusy(st, 1010));
}

static void testIdleLoops()
{
  // A pet that has just become idle stands still at the centre first.
  PpStage st;
  idleStage(st, 1000);
  PpStagePose p = at(st, 1000);
  CHECK(p.anim == PP_SANIM_IDLE && p.frame == 0 && p.dx == 0 && p.dy == 0);
  CHECK(p.scene == PP_SCENE_MEADOW && !p.emptyBall && !p.prop);
  p = at(st, 1500);
  CHECK(p.anim == PP_SANIM_IDLE && p.frame == 0 && p.dx == 0);
}

static void testChangedOnlyOnChange()
{
  PpStage st;
  idleStage(st, 0);
  PpStagePose p;
  CHECK(ppStagePose(st, T, 0, p));
  CHECK(!ppStagePose(st, T, 50, p));
  CHECK(!ppStagePose(st, T, 300, p));
}

static void testAsleepPose()
{
  PpStage st;
  ppStageInit(st, 0);
  ppStageSetBase(st, PP_STAGE_ASLEEP, 0);
  CHECK(at(st, 0).anim == PP_SANIM_SLEEP);
  CHECK(at(st, 0).frame == 0);
  CHECK(at(st, 499).frame == 0);
  CHECK(at(st, 500).frame == 1);
  CHECK(at(st, 1099).frame == 1);
  CHECK(at(st, 1100).frame == 0);
  PpStagePose p = at(st, 0);
  CHECK(p.scene == PP_SCENE_MEADOW_NIGHT);
  CHECK(p.symbol == PP_SYM_ZZZ);
  CHECK(p.symbolProgress == 0);
  CHECK(at(st, 1500).symbolProgress == 500);
  CHECK(at(st, 2997).symbolProgress == 999);
  CHECK(at(st, 3000).symbolProgress == 0);
}

static void testAnimationsOffIsStill()
{
  PpStage st;
  idleStage(st, 0);
  ppStageSetAnimations(st, false);
  PpStagePose p = {};
  CHECK(ppStagePose(st, T, 0, p));
  CHECK(p.anim == PP_SANIM_IDLE && p.frame == 0 && p.symbol == PP_SYM_NONE);
  CHECK(p.scene == PP_SCENE_MEADOW);
  for (uint32_t t = 100; t <= 10000; t += 100) {
    CHECK(!ppStagePose(st, T, t, p));
  }
  ppStageReact(st, PP_REACT_HAPPY, 10000);
  CHECK(!ppStageBusy(st, 10001));
  CHECK(!ppStagePose(st, T, 10001, p));
  ppStageStartPlay(st, PP_SCENE_BEACH, 10000);
  CHECK(!ppStageBusy(st, 10001));
  CHECK(!ppStagePose(st, T, 10001, p));

  ppStageSetBase(st, PP_STAGE_ASLEEP, 11000);
  CHECK(ppStagePose(st, T, 11000, p));
  CHECK(p.anim == PP_SANIM_IDLE && p.frame == 0 && p.symbol == PP_SYM_NONE);
  CHECK(p.scene == PP_SCENE_MEADOW_NIGHT);
  for (uint32_t t = 11100; t <= 21000; t += 100) {
    CHECK(!ppStagePose(st, T, t, p));
  }
}

static void testReactionLengths()
{
  CHECK(ppStageReactionMs(PP_REACT_HAPPY) == 1500);
  CHECK(ppStageReactionMs(PP_REACT_FLOP) == 2000);
  CHECK(ppStageReactionMs(PP_REACT_BERRY) == 1400);
  CHECK(ppStageReactionMs(PP_REACT_BERRY_FULL) == 1400);
  CHECK(ppStageReactionMs(PP_REACT_ALERT) == 1000);
  CHECK(ppStageReactionMs(PP_REACT_WAKE) == 800);
  CHECK(ppStageReactionMs(PP_REACT_NONE) == 0);
}

static void testHappyThenBackToIdle()
{
  PpStage st;
  idleStage(st, 0);
  ppStageReact(st, PP_REACT_HAPPY, 1000);
  PpStagePose p = at(st, 1000);
  CHECK(p.anim == PP_SANIM_HOP && p.symbol == PP_SYM_HEARTS && p.symbolProgress == 0);
  CHECK(ppStageBusy(st, 1000));
  p = at(st, 2499);
  CHECK(p.anim == PP_SANIM_HOP && p.symbol == PP_SYM_HEARTS);
  CHECK(p.symbolProgress == 999);
  CHECK(ppStageBusy(st, 2499));
  p = at(st, 2500);
  CHECK(p.anim == PP_SANIM_IDLE && p.symbol == PP_SYM_NONE);
  CHECK(!ppStageBusy(st, 2500));
  // hop frames loop: 50 ms each
  CHECK(at(st, 1000).frame == 0);
  ppStageReact(st, PP_REACT_HAPPY, 5000);
  CHECK(at(st, 5120).frame == 2);
}

static void testReactionReplaces()
{
  PpStage st;
  idleStage(st, 0);
  ppStageReact(st, PP_REACT_HAPPY, 1000);
  ppStageReact(st, PP_REACT_FLOP, 1300);
  PpStagePose p = at(st, 1300);
  CHECK(p.symbol == PP_SYM_NONE && !p.tipped && p.anim == PP_SANIM_IDLE);
  p = at(st, 1300 + 300);
  CHECK(p.tipped);
  p = at(st, 1300 + 400);
  CHECK(p.symbol == PP_SYM_STARS);
  p = at(st, 1300 + 2000);
  CHECK(!p.tipped && p.symbol == PP_SYM_NONE);
  CHECK(!ppStageBusy(st, 1300 + 2000));
  // Happy would have ended at 2500; Flop is still playing then
  CHECK(ppStageBusy(st, 2600));
  CHECK(at(st, 2600).symbol == PP_SYM_STARS);
}

static void testFlopTips()
{
  PpStage st;
  idleStage(st, 0);
  ppStageReact(st, PP_REACT_FLOP, 1000);
  CHECK(!at(st, 1299).tipped);
  CHECK(at(st, 1300).tipped);
  CHECK(at(st, 1699).tipped);
  CHECK(at(st, 2699).tipped);
  CHECK(!at(st, 2700).tipped);
  CHECK(at(st, 1299).dy == 0);
  CHECK(at(st, 1300).dy == -6);
  CHECK(at(st, 1499).dy == -6);
  CHECK(at(st, 1500).dy == 0);
  CHECK(at(st, 1399).symbol == PP_SYM_NONE);
  CHECK(at(st, 1400).symbol == PP_SYM_STARS);
  CHECK(at(st, 1400).symbolProgress == 200);
  CHECK(at(st, 1500).frame == 0 && at(st, 1500).anim == PP_SANIM_IDLE);
  CHECK(at(st, 1999).frame == 0);
}

static void testBerryHopsAfterDrop()
{
  PpStage st;
  idleStage(st, 0);
  ppStageReact(st, PP_REACT_BERRY, 1000);
  CHECK(at(st, 1000).anim == PP_SANIM_IDLE);
  CHECK(at(st, 1599).anim == PP_SANIM_IDLE);
  CHECK(at(st, 1600).anim == PP_SANIM_HOP);
  CHECK(at(st, 1600).frame == 0);
  CHECK(at(st, 1650).frame == 1);
  CHECK(at(st, 1000).symbol == PP_SYM_BERRY);
  CHECK(at(st, 1599).symbol == PP_SYM_BERRY);
  CHECK(at(st, 2399).symbol == PP_SYM_BERRY);
  CHECK(at(st, 2400).symbol == PP_SYM_NONE);
}

static void testBerryFull()
{
  PpStage st;
  idleStage(st, 0);
  ppStageReact(st, PP_REACT_BERRY_FULL, 1000);
  for (uint32_t t = 1000; t < 2400; t += 50) {
    PpStagePose p = at(st, t);
    CHECK(p.anim == PP_SANIM_IDLE);
    CHECK(p.symbol == PP_SYM_BERRY_BOUNCE);
  }
  CHECK(at(st, 1700).symbolProgress == 500);
}

static void testAlert()
{
  PpStage st;
  idleStage(st, 0);
  ppStageReact(st, PP_REACT_ALERT, 1000);
  CHECK(at(st, 1000).dy == -8);
  CHECK(at(st, 1249).dy == -8);
  CHECK(at(st, 1250).dy == 0);
  CHECK(at(st, 1500).symbol == PP_SYM_ALERT);
  CHECK(at(st, 1999).symbol == PP_SYM_ALERT);
  CHECK(at(st, 1250).frame == 0);
  CHECK(at(st, 1800).frame == 0);
  CHECK(at(st, 1800).anim == PP_SANIM_IDLE);
  CHECK(at(st, 2000).symbol == PP_SYM_NONE);
}

static void testWakeSetsIdle()
{
  PpStage st;
  ppStageInit(st, 0);
  ppStageSetBase(st, PP_STAGE_ASLEEP, 0);
  ppStageReact(st, PP_REACT_WAKE, 1000);
  CHECK(st.base == PP_STAGE_IDLE);
  PpStagePose p = at(st, 1000);
  CHECK(p.scene == PP_SCENE_MEADOW);
  CHECK(p.anim == PP_SANIM_HOP);
  CHECK(p.symbol == PP_SYM_NONE);
  CHECK(at(st, 1799).anim == PP_SANIM_HOP);
  CHECK(ppStageBusy(st, 1799));
  CHECK(at(st, 1800).anim == PP_SANIM_IDLE);
  CHECK(!ppStageBusy(st, 1800));
}

static void testNoReactionWhenEmpty()
{
  PpStage st;
  ppStageInit(st, 0);
  ppStageReact(st, PP_REACT_HAPPY, 20);
  PpStagePose p = {};
  CHECK(ppStagePose(st, T, 10, p));
  CHECK(!ppStagePose(st, T, 30, p));
  CHECK(p.emptyBall);
  CHECK(!ppStageBusy(st, 30));
  ppStageReact(st, PP_REACT_WAKE, 40);
  CHECK(st.base == PP_STAGE_EMPTY);
}

static void testBaseChangeDropsReaction()
{
  PpStage st;
  idleStage(st, 0);
  ppStageReact(st, PP_REACT_HAPPY, 1000);
  ppStageSetBase(st, PP_STAGE_ASLEEP, 1100);
  PpStagePose p = at(st, 1100);
  CHECK(p.anim == PP_SANIM_SLEEP && p.symbol == PP_SYM_ZZZ);
  CHECK(!ppStageBusy(st, 1100));
}

static void testPlayWalk()
{
  // Play is a chase: a prop moves through the scene and the pet runs after it.
  PpStage st;
  idleStage(st, 0);
  ppStageStartPlay(st, PP_SCENE_BEACH, 1000);
  PpStagePose p = at(st, 1000);
  CHECK(p.scene == PP_SCENE_BEACH && p.prop && p.propKind == PP_PROP_BALL);
  bool sawLeft = false, sawRight = false, sawWalk = false;
  for (uint32_t e = 0; e <= 5000; e += 50) {
    p = at(st, 1000 + e);
    CHECK(p.prop && p.scene == PP_SCENE_BEACH);
    CHECK(p.dx >= -70 && p.dx <= 70);
    CHECK(p.propX >= -60 && p.propX <= 60);
    CHECK(p.propY < 0);
    if (e < 4200) {
      CHECK(p.symbol == PP_SYM_NONE);
      CHECK(p.anim == PP_SANIM_WALK || (p.anim == PP_SANIM_IDLE && p.frame == 0));
      if (p.anim == PP_SANIM_WALK) {
        sawWalk = true;
        if (p.mirror) {
          sawLeft = true;
        } else {
          sawRight = true;
        }
      }
    } else {
      CHECK(p.anim == PP_SANIM_HOP);
    }
  }
  CHECK(sawWalk && sawLeft && sawRight);
  // By the time it jumps, the pet has caught up with the prop.
  p = at(st, 1000 + 4200);
  int gap = p.dx - p.propX;
  CHECK(gap >= -16 && gap <= 16);
  CHECK(at(st, 1000 + 4600).symbol == PP_SYM_HEARTS);
  CHECK(!ppStageMomentDone(st, 1000 + 4999));
  CHECK(ppStageMomentDone(st, 1000 + 5000));
  CHECK(ppStageBusy(st, 1000 + 4999));
  // holds its last pose until cancelled
  PpStagePose last = at(st, 1000 + 5000);
  p = at(st, 1000 + 9000);
  CHECK(p.dx == last.dx && p.scene == PP_SCENE_BEACH && p.prop);
}

static void testChaseProps()
{
  PpStage st;
  idleStage(st, 0);
  CHECK(ppStagePropFor(PP_SCENE_MEADOW) == PP_PROP_BUTTERFLY);
  CHECK(ppStagePropFor(PP_SCENE_BEACH) == PP_PROP_BALL);
  CHECK(ppStagePropFor(PP_SCENE_FOREST) == PP_PROP_LEAF);
  CHECK(ppStagePropFor(PP_SCENE_CAMP_NIGHT) == PP_PROP_FIREFLY);
  ppStageStartPlay(st, PP_SCENE_CAMP_NIGHT, 100);
  CHECK(at(st, 200).propKind == PP_PROP_FIREFLY);
  // The same moment of a chase always looks the same.
  PpStage a, b;
  idleStage(a, 0);
  idleStage(b, 500);
  ppStageStartPlay(a, PP_SCENE_MEADOW, 1000);
  ppStageStartPlay(b, PP_SCENE_MEADOW, 9000);
  for (uint32_t e = 0; e <= 5000; e += 250) {
    PpStagePose pa = at(a, 1000 + e), pb = at(b, 9000 + e);
    CHECK(pa.dx == pb.dx && pa.propX == pb.propX && pa.propY == pb.propY && pa.anim == pb.anim && pa.mirror == pb.mirror);
  }
  // No prop outside play.
  ppStageCancel(a);
  CHECK(!at(a, 20000).prop);
}

static void testWanderStaysOnStage()
{
  PpStage st;
  idleStage(st, 1000);
  st.rng = 12345;
  bool sawWalk = false, sawStill = false, sawBob = false, sawLeft = false, sawRight = false;
  int16_t prevDx = 0;
  bool first = true;
  for (uint32_t t = 1000; t < 1000 + 300000; t += 50) {
    PpStagePose p = at(st, t);
    CHECK(p.dx >= -PP_STAGE_WALK_SPAN && p.dx <= PP_STAGE_WALK_SPAN);
    CHECK(p.dy == 0 && !p.tipped && p.symbol == PP_SYM_NONE && !p.prop);
    CHECK(p.anim == PP_SANIM_IDLE || p.anim == PP_SANIM_WALK);
    if (p.anim == PP_SANIM_WALK) {
      sawWalk = true;
      if (!first && p.dx > prevDx) {
        CHECK(!p.mirror);
        sawRight = true;
      }
      if (!first && p.dx < prevDx) {
        CHECK(p.mirror);
        sawLeft = true;
      }
    } else {
      if (p.frame == 0) {
        sawStill = true;
      } else {
        sawBob = true;
      }
    }
    prevDx = p.dx;
    first = false;
  }
  CHECK(sawWalk && sawStill && sawBob && sawLeft && sawRight);
}

static void testWanderDeterministic()
{
  PpStage a, b;
  idleStage(a, 1000);
  idleStage(b, 1000);
  a.rng = 777;
  b.rng = 777;
  for (uint32_t t = 1000; t < 61000; t += 100) {
    PpStagePose pa = at(a, t), pb = at(b, t);
    CHECK(pa.dx == pb.dx && pa.anim == pb.anim && pa.frame == pb.frame && pa.mirror == pb.mirror);
  }
}

static void testNudgeStartsWalking()
{
  PpStage st;
  idleStage(st, 1000);
  st.rng = 99;
  ppStageNudge(st, 5000);
  PpStagePose p = at(st, 5200);
  CHECK(p.anim == PP_SANIM_WALK);
  // Nudging an empty, asleep, still or busy stage does nothing harmful.
  PpStage e;
  ppStageInit(e, 0);
  ppStageNudge(e, 100);
  CHECK(at(e, 200).emptyBall);
  PpStage off;
  idleStage(off, 0);
  ppStageSetAnimations(off, false);
  ppStageNudge(off, 100);
  p = at(off, 500);
  CHECK(p.anim == PP_SANIM_IDLE && p.frame == 0 && p.dx == 0);
}

static void testReactionKeepsSpot()
{
  PpStage st;
  idleStage(st, 1000);
  st.rng = 4242;
  uint32_t t = 1000;
  PpStagePose p = at(st, t);
  while (t < 120000 && p.dx == 0) {
    t += 50;
    p = at(st, t);
  }
  CHECK(p.dx != 0);
  int16_t spot = p.dx;
  ppStageReact(st, PP_REACT_HAPPY, t);
  for (uint32_t e = 0; e < 1500; e += 100) {
    p = at(st, t + e);
    CHECK(p.dx == spot && p.anim == PP_SANIM_HOP);
  }
  // After the reaction it carries on from where it stood.
  p = at(st, t + 1500);
  CHECK(p.dx == spot && p.anim == PP_SANIM_IDLE);
}

static void testPlayIgnoresReactions()
{
  PpStage st;
  idleStage(st, 0);
  ppStageStartPlay(st, PP_SCENE_FOREST, 1000);
  ppStageReact(st, PP_REACT_HAPPY, 1500);
  PpStagePose p = at(st, 1600);
  CHECK(p.prop && p.symbol == PP_SYM_NONE && p.scene == PP_SCENE_FOREST && p.anim != PP_SANIM_HOP);
}

static void testEvolveFlicker()
{
  PpStage st;
  idleStage(st, 0);
  ppStageStartEvolve(st, 1000);
  CHECK(at(st, 1000).oldForm);
  CHECK(at(st, 1249).oldForm);
  CHECK(!at(st, 1250).oldForm);
  CHECK(at(st, 1500).oldForm);
  CHECK(!at(st, 1000 + 1250).oldForm);
  CHECK(at(st, 1000 + 1500).oldForm);
  CHECK(!at(st, 1000 + 1625).oldForm);
  CHECK(at(st, 1000 + 1750).oldForm);
  CHECK(!at(st, 1000 + 1875).oldForm);
  for (uint32_t e = 2000; e <= 2500; e += 25) {
    CHECK(!at(st, 1000 + e).oldForm);
  }
  PpStagePose p = at(st, 1000);
  CHECK(p.anim == PP_SANIM_IDLE && p.frame == 0 && p.scene == PP_SCENE_MEADOW);
  CHECK(!ppStageMomentDone(st, 1000 + 2499));
  CHECK(ppStageMomentDone(st, 1000 + 2500));
}

static void testCancel()
{
  PpStage st;
  idleStage(st, 0);
  ppStageStartPlay(st, PP_SCENE_BEACH, 1000);
  CHECK(at(st, 1500).anim == PP_SANIM_WALK);
  ppStageCancel(st);
  PpStagePose p = {};
  CHECK(ppStagePose(st, T, 1500, p));
  // Back to the wander on the home scene: no prop, standing or walking.
  CHECK(!p.prop && p.scene == PP_SCENE_MEADOW && (p.anim == PP_SANIM_IDLE || p.anim == PP_SANIM_WALK));
  CHECK(!ppStageMomentDone(st, 9000));
  CHECK(!ppStageBusy(st, 1500));
}

static void testMissingAnimationFallsBack()
{
  PpStageTimings noHop = fixture();
  noHop.anim[PP_SANIM_HOP].frames = 0;
  PpStage st;
  idleStage(st, 0);
  ppStageReact(st, PP_REACT_HAPPY, 1000);
  PpStagePose p = {};
  ppStagePose(st, noHop, 1300, p);
  CHECK(p.anim == PP_SANIM_IDLE && p.frame == 2);
  CHECK(p.symbol == PP_SYM_HEARTS);

  PpStageTimings none = {};
  PpStage s2;
  idleStage(s2, 0);
  ppStageReact(s2, PP_REACT_HAPPY, 1000);
  ppStagePose(s2, none, 1300, p);
  CHECK(p.anim == PP_SANIM_IDLE && p.frame == 0);
  ppStageStartPlay(s2, PP_SCENE_BEACH, 1000);
  ppStagePose(s2, none, 1300, p);
  CHECK(p.frame == 0);

  PpStageTimings nullMs = fixture();
  nullMs.anim[PP_SANIM_SLEEP].ms = nullptr;
  PpStage s3;
  ppStageInit(s3, 0);
  ppStageSetBase(s3, PP_STAGE_ASLEEP, 0);
  ppStagePose(s3, nullMs, 700, p);
  CHECK(p.anim == PP_SANIM_IDLE && p.frame == 1);

  PpStageTimings zeroTotal = fixture();
  static const uint16_t zeros[2] = {0, 0};
  zeroTotal.anim[PP_SANIM_SLEEP] = {2, zeros};
  ppStagePose(s3, zeroTotal, 700, p);
  CHECK(p.anim == PP_SANIM_IDLE);
}

static void testAcrossWrap()
{
  // The wander keeps going, in bounds, across the millis() wrap.
  PpStage st;
  idleStage(st, 0xFFFFFF00u);
  st.rng = 31337;
  bool sawWalk = false;
  for (uint32_t e = 0; e < 60000; e += 50) {
    PpStagePose p = at(st, 0xFFFFFF00u + e);
    CHECK(p.dx >= -PP_STAGE_WALK_SPAN && p.dx <= PP_STAGE_WALK_SPAN);
    if (p.anim == PP_SANIM_WALK) {
      sawWalk = true;
    }
  }
  CHECK(sawWalk);

  PpStage s2;
  idleStage(s2, 0xFFFFFD00u);
  ppStageReact(s2, PP_REACT_HAPPY, 0xFFFFFE00u);
  CHECK(ppStageBusy(s2, 0xFFFFFE00u));
  CHECK(at(s2, 0xFFFFFE00u + 10).anim == PP_SANIM_HOP);
  CHECK(ppStageBusy(s2, 0xFFFFFE00u + 1499));
  CHECK(at(s2, 0xFFFFFE00u + 1499).anim == PP_SANIM_HOP);
  CHECK(!ppStageBusy(s2, 0xFFFFFE00u + 1500));
  CHECK(at(s2, 0xFFFFFE00u + 1500).anim == PP_SANIM_IDLE);
}

static void testStaleReactionNeverShows()
{
  PpStage st;
  idleStage(st, 0);
  ppStageReact(st, PP_REACT_HAPPY, 1000);
  PpStagePose p = at(st, 61000);
  CHECK(p.anim == PP_SANIM_IDLE && p.symbol == PP_SYM_NONE);
  CHECK(!ppStageBusy(st, 61000));
}

static void testBaseFor()
{
  CHECK(ppStageBaseFor(false, false) == PP_STAGE_EMPTY);
  CHECK(ppStageBaseFor(false, true) == PP_STAGE_EMPTY);
  CHECK(ppStageBaseFor(true, false) == PP_STAGE_ASLEEP);
  CHECK(ppStageBaseFor(true, true) == PP_STAGE_IDLE);
}

static void testGoReadyFollowsReadyAndDisconnect()
{
  bool ready = false;
  // The raw connect does not wake the pet; only READY does.
  ready = ppStageGoReadyAfter(ready, PP_FEEDBACK_CONNECTED);
  CHECK(!ready);
  ready = ppStageGoReadyAfter(ready, PP_FEEDBACK_READY);
  CHECK(ready);
  ready = ppStageGoReadyAfter(ready, PP_FEEDBACK_CAUGHT);
  CHECK(ready);
  ready = ppStageGoReadyAfter(ready, PP_FEEDBACK_CONNECTED);
  CHECK(ready);
  ready = ppStageGoReadyAfter(ready, PP_FEEDBACK_DISCONNECTED);
  CHECK(!ready);
  ready = ppStageGoReadyAfter(ready, PP_FEEDBACK_POKESTOP_SPUN);
  CHECK(!ready);
  ready = ppStageGoReadyAfter(ready, PP_FEEDBACK_CONNECTED);
  CHECK(!ready);
}

static void testLoopFrame()
{
  const PpAnimTiming idle = {4, kIdleMs};   // 200, 100, 100, 100: 500 ms a loop
  CHECK(ppStageLoopFrame(idle, 0) == 0);
  CHECK(ppStageLoopFrame(idle, 199) == 0);
  CHECK(ppStageLoopFrame(idle, 200) == 1);
  CHECK(ppStageLoopFrame(idle, 299) == 1);
  CHECK(ppStageLoopFrame(idle, 300) == 2);
  CHECK(ppStageLoopFrame(idle, 499) == 3);
  CHECK(ppStageLoopFrame(idle, 500) == 0);
  CHECK(ppStageLoopFrame(idle, 700) == 1);
  CHECK(ppStageLoopFrame(idle, 0xffffffffu) == ppStageLoopFrame(idle, 0xffffffffu % 500));
  // Nothing usable: frame 0, never an index past the end.
  const PpAnimTiming none = {0, nullptr};
  CHECK(ppStageLoopFrame(none, 1234) == 0);
  const PpAnimTiming noTimes = {3, nullptr};
  CHECK(ppStageLoopFrame(noTimes, 1234) == 0);
  static const uint16_t kZero[2] = {0, 0};
  const PpAnimTiming zero = {2, kZero};
  CHECK(ppStageLoopFrame(zero, 1234) == 0);
}

static void testPlaySceneSpreads()
{
  uint32_t counts[PP_SCENE_PLAY_COUNT] = {};
  bool inRange = true;
  for (uint32_t seed = 0; seed < 10000; ++seed) {
    PpStageScene s = ppStagePlayScene(seed);
    if (s >= PP_SCENE_PLAY_COUNT) {
      inRange = false;
      continue;
    }
    ++counts[s];
  }
  CHECK(inRange);
  for (uint8_t i = 0; i < PP_SCENE_PLAY_COUNT; ++i) {
    CHECK(counts[i] > 2000 && counts[i] < 3000);
  }
  // Seeds that step like a coarse microsecond clock (about 977 us a tick) still reach all four.
  uint32_t coarse[PP_SCENE_PLAY_COUNT] = {};
  for (uint32_t tick = 0; tick < 400; ++tick) {
    PpStageScene s = ppStagePlayScene(tick * 977u);
    if (s < PP_SCENE_PLAY_COUNT) {
      ++coarse[s];
    }
  }
  for (uint8_t i = 0; i < PP_SCENE_PLAY_COUNT; ++i) {
    CHECK(coarse[i] > 50);
  }
  // The same seed gives the same scene: the pick is a pure function, not a game roll.
  CHECK(ppStagePlayScene(123456u) == ppStagePlayScene(123456u));
}

int main()
{
  testBaseFor();
  testGoReadyFollowsReadyAndDisconnect();
  testInitIsEmpty();
  testIdleLoops();
  testChangedOnlyOnChange();
  testAsleepPose();
  testAnimationsOffIsStill();
  testReactionLengths();
  testHappyThenBackToIdle();
  testReactionReplaces();
  testFlopTips();
  testBerryHopsAfterDrop();
  testBerryFull();
  testAlert();
  testWakeSetsIdle();
  testNoReactionWhenEmpty();
  testBaseChangeDropsReaction();
  testPlayWalk();
  testChaseProps();
  testWanderStaysOnStage();
  testWanderDeterministic();
  testNudgeStartsWalking();
  testReactionKeepsSpot();
  testPlayIgnoresReactions();
  testEvolveFlicker();
  testCancel();
  testMissingAnimationFallsBack();
  testAcrossWrap();
  testStaleReactionNeverShows();
  testLoopFrame();
  testPlaySceneSpreads();
  if (failures == 0) {
    std::printf("gachi_stage: all passed\n");
    return 0;
  }
  std::printf("gachi_stage: %d failure(s)\n", failures);
  return 1;
}
