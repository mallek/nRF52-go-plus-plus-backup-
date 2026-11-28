#include "pp_gachi_stage.h"

namespace {

constexpr uint32_t kFlopTipStart = 300;
constexpr uint32_t kFlopTipEnd = 1700;
constexpr uint32_t kFlopHopEnd = 500;
constexpr uint32_t kFlopStarsStart = 400;
constexpr uint32_t kBerryHopStart = 600;
constexpr uint32_t kAlertRiseMs = 250;
constexpr int16_t kFlopDy = -6;
constexpr int16_t kAlertDy = -8;
constexpr uint32_t kZzzPeriodMs = 3000;

uint32_t elapsedSince(uint32_t now, uint32_t start)
{
  return (uint32_t)(now - start);
}

uint32_t momentLength(PpStageMoment m)
{
  if (m == PP_MOMENT_PLAY) {
    return PP_STAGE_PLAY_MS;
  }
  if (m == PP_MOMENT_EVOLVE) {
    return PP_STAGE_EVOLVE_MS;
  }
  return 0;
}

// Total loop length of an animation, or 0 when it cannot be used.
uint32_t loopTotal(const PpAnimTiming& t)
{
  if (t.frames == 0 || t.ms == nullptr) {
    return 0;
  }
  uint32_t total = 0;
  for (uint8_t i = 0; i < t.frames; ++i) {
    total += t.ms[i];
  }
  return total;
}

// Resolves `want` looping at `elapsed` into an animation that can be drawn.
void resolveLoop(const PpStageTimings& timings, PpStageAnim want, uint32_t elapsed, PpStagePose& pose)
{
  PpStageAnim use = want;
  if (loopTotal(timings.anim[use]) == 0) {
    use = PP_SANIM_IDLE;
  }
  pose.anim = use;
  pose.frame = ppStageLoopFrame(timings.anim[use], elapsed);
}

bool reactionActive(const PpStage& st, uint32_t now)
{
  if (st.reaction == PP_REACT_NONE) {
    return false;
  }
  return elapsedSince(now, st.reactionAt) < ppStageReactionMs(st.reaction);
}

bool poseEqual(const PpStagePose& a, const PpStagePose& b)
{
  return a.scene == b.scene && a.emptyBall == b.emptyBall && a.oldForm == b.oldForm && a.anim == b.anim &&
         a.frame == b.frame && a.dx == b.dx && a.dy == b.dy && a.mirror == b.mirror && a.tipped == b.tipped &&
         a.symbol == b.symbol && a.symbolProgress == b.symbolProgress;
}

PpStagePose blankPose(PpStageScene scene)
{
  PpStagePose p = {};
  p.scene = scene;
  p.anim = PP_SANIM_IDLE;
  p.symbol = PP_SYM_NONE;
  return p;
}

}  // namespace

void ppStageInit(PpStage& st, uint32_t now)
{
  st = PpStage();
  st.base = PP_STAGE_EMPTY;
  st.baseAt = now;
  st.animationsOn = true;
  st.reaction = PP_REACT_NONE;
  st.moment = PP_MOMENT_NONE;
  st.momentScene = PP_SCENE_MEADOW;
  st.hasLast = false;
}

void ppStageSetBase(PpStage& st, PpStageBase base, uint32_t now)
{
  if (st.base == base) {
    return;
  }
  st.base = base;
  st.baseAt = now;
  st.reaction = PP_REACT_NONE;
  st.hasLast = false;
}

void ppStageSetAnimations(PpStage& st, bool on)
{
  st.animationsOn = on;
  if (!on) {
    st.reaction = PP_REACT_NONE;
    st.moment = PP_MOMENT_NONE;
  }
  st.hasLast = false;
}

void ppStageReact(PpStage& st, PpPetReaction reaction, uint32_t now)
{
  if (reaction == PP_REACT_NONE || st.base == PP_STAGE_EMPTY || !st.animationsOn || st.moment != PP_MOMENT_NONE) {
    return;
  }
  if (reaction == PP_REACT_WAKE) {
    ppStageSetBase(st, PP_STAGE_IDLE, now);
  }
  st.reaction = reaction;
  st.reactionAt = now;
  st.hasLast = false;
}

void ppStageStartPlay(PpStage& st, PpStageScene scene, uint32_t now)
{
  if (!st.animationsOn) {
    return;
  }
  st.reaction = PP_REACT_NONE;
  st.moment = PP_MOMENT_PLAY;
  st.momentAt = now;
  st.momentScene = scene;
  st.hasLast = false;
}

void ppStageStartEvolve(PpStage& st, uint32_t now)
{
  if (!st.animationsOn) {
    return;
  }
  st.reaction = PP_REACT_NONE;
  st.moment = PP_MOMENT_EVOLVE;
  st.momentAt = now;
  st.momentScene = PP_SCENE_MEADOW;
  st.hasLast = false;
}

void ppStageCancel(PpStage& st)
{
  st.reaction = PP_REACT_NONE;
  st.moment = PP_MOMENT_NONE;
  st.hasLast = false;
}

uint32_t ppStageReactionMs(PpPetReaction reaction)
{
  switch (reaction) {
    case PP_REACT_HAPPY:
      return 1500;
    case PP_REACT_FLOP:
      return 2000;
    case PP_REACT_BERRY:
      return 1400;
    case PP_REACT_BERRY_FULL:
      return 1400;
    case PP_REACT_ALERT:
      return 1000;
    case PP_REACT_WAKE:
      return 800;
    case PP_REACT_NONE:
    default:
      return 0;
  }
}

bool ppStageBusy(const PpStage& st, uint32_t now)
{
  if (st.moment != PP_MOMENT_NONE && elapsedSince(now, st.momentAt) < momentLength(st.moment)) {
    return true;
  }
  return reactionActive(st, now);
}

bool ppStageMomentDone(const PpStage& st, uint32_t now)
{
  return st.moment != PP_MOMENT_NONE && elapsedSince(now, st.momentAt) >= momentLength(st.moment);
}

bool ppStagePose(PpStage& st, const PpStageTimings& timings, uint32_t now, PpStagePose& out)
{
  const PpStageScene baseScene = (st.base == PP_STAGE_ASLEEP) ? PP_SCENE_MEADOW_NIGHT : PP_SCENE_MEADOW;
  PpStagePose p = blankPose(baseScene);

  if (st.base == PP_STAGE_EMPTY) {
    p.scene = PP_SCENE_MEADOW;
    p.emptyBall = true;
  } else if (st.moment == PP_MOMENT_PLAY) {
    uint32_t e = elapsedSince(now, st.momentAt);
    if (e > PP_STAGE_PLAY_MS) {
      e = PP_STAGE_PLAY_MS;
    }
    const uint32_t half = PP_STAGE_PLAY_MS / 2;
    const int32_t span = PP_STAGE_WALK_SPAN;
    p.scene = st.momentScene;
    resolveLoop(timings, PP_SANIM_WALK, e, p);
    if (e <= half) {
      p.dx = (int16_t)(-span + (int32_t)((2 * span * e) / half));
    } else {
      p.dx = (int16_t)(span - (int32_t)((2 * span * (e - half)) / half));
      p.mirror = true;
    }
  } else if (st.moment == PP_MOMENT_EVOLVE) {
    uint32_t e = elapsedSince(now, st.momentAt);
    if (e > PP_STAGE_EVOLVE_MS) {
      e = PP_STAGE_EVOLVE_MS;
    }
    if (e < 1500) {
      p.oldForm = ((e / 250) % 2) == 0;
    } else if (e < 2000) {
      p.oldForm = (((e - 1500) / 125) % 2) == 0;
    } else {
      p.oldForm = false;
    }
  } else if (!st.animationsOn) {
    // still: Idle frame 0, no symbol
  } else if (reactionActive(st, now)) {
    const uint32_t e = elapsedSince(now, st.reactionAt);
    const uint32_t len = ppStageReactionMs(st.reaction);
    const uint16_t progress = (uint16_t)((e * 1000u) / len);
    // Idle loops keep the base's phase so the pet does not restart its idle on a reaction.
    const uint32_t idleE = elapsedSince(now, st.baseAt);
    switch (st.reaction) {
      case PP_REACT_HAPPY:
        resolveLoop(timings, PP_SANIM_HOP, e, p);
        p.symbol = PP_SYM_HEARTS;
        p.symbolProgress = progress;
        break;
      case PP_REACT_FLOP:
        p.tipped = e >= kFlopTipStart && e < kFlopTipEnd;
        if (e >= kFlopTipStart && e < kFlopHopEnd) {
          p.dy = kFlopDy;
        }
        if (e >= kFlopStarsStart) {
          p.symbol = PP_SYM_STARS;
          p.symbolProgress = progress;
        }
        break;
      case PP_REACT_BERRY:
        if (e < kBerryHopStart) {
          resolveLoop(timings, PP_SANIM_IDLE, idleE, p);
        } else {
          resolveLoop(timings, PP_SANIM_HOP, e - kBerryHopStart, p);
        }
        p.symbol = PP_SYM_BERRY;
        p.symbolProgress = progress;
        break;
      case PP_REACT_BERRY_FULL:
        resolveLoop(timings, PP_SANIM_IDLE, idleE, p);
        p.symbol = PP_SYM_BERRY_BOUNCE;
        p.symbolProgress = progress;
        break;
      case PP_REACT_ALERT:
        if (e < kAlertRiseMs) {
          p.dy = kAlertDy;
        }
        p.symbol = PP_SYM_ALERT;
        p.symbolProgress = progress;
        break;
      case PP_REACT_WAKE:
        p.scene = PP_SCENE_MEADOW;
        resolveLoop(timings, PP_SANIM_HOP, e, p);
        break;
      case PP_REACT_NONE:
      default:
        break;
    }
  } else if (st.base == PP_STAGE_ASLEEP) {
    const uint32_t e = elapsedSince(now, st.baseAt);
    resolveLoop(timings, PP_SANIM_SLEEP, e, p);
    p.symbol = PP_SYM_ZZZ;
    p.symbolProgress = (uint16_t)((e % kZzzPeriodMs) / 3);
  } else {
    resolveLoop(timings, PP_SANIM_IDLE, elapsedSince(now, st.baseAt), p);
  }

  const bool changed = !st.hasLast || !poseEqual(st.last, p);
  st.last = p;
  st.hasLast = true;
  out = p;
  return changed;
}

uint8_t ppStageLoopFrame(const PpAnimTiming& t, uint32_t elapsed)
{
  uint32_t total = loopTotal(t);
  if (total == 0) {
    return 0;
  }
  uint32_t pos = elapsed % total;
  uint32_t acc = 0;
  for (uint8_t i = 0; i < t.frames; ++i) {
    acc += t.ms[i];
    if (pos < acc) {
      return i;
    }
  }
  return 0;
}

PpStageScene ppStagePlayScene(uint32_t seed)
{
  // A 32-bit finalising mix, so low-bit patterns in the seed do not pick the scene.
  uint32_t x = seed;
  x ^= x >> 16;
  x *= 0x85ebca6bu;
  x ^= x >> 13;
  x *= 0xc2b2ae35u;
  x ^= x >> 16;
  return static_cast<PpStageScene>(x % PP_SCENE_PLAY_COUNT);
}

PpStageBase ppStageBaseFor(bool hasPet, bool goReady)
{
  if (!hasPet) {
    return PP_STAGE_EMPTY;
  }
  return goReady ? PP_STAGE_IDLE : PP_STAGE_ASLEEP;
}

bool ppStageGoReadyAfter(bool goReady, PpFeedbackEvent event)
{
  if (event == PP_FEEDBACK_READY) {
    return true;
  }
  if (event == PP_FEEDBACK_DISCONNECTED) {
    return false;
  }
  return goReady;
}
