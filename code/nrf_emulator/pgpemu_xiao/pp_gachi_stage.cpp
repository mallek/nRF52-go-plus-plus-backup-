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

// Idle wander.
enum : uint8_t { kWanderStand, kWanderBob, kWanderWalk };
constexpr uint32_t kWanderFirstStandMs = 600;
constexpr uint32_t kWanderStandMinMs = 800;
constexpr uint32_t kWanderStandSpreadMs = 1700;
constexpr int32_t kWanderSpeed = 30;        // 240-wide units per second
constexpr int16_t kWanderMinStep = 20;
constexpr uint32_t kReactionSettleMs = 400; // stands a moment after a reaction before wandering on

// Play chase.
constexpr uint32_t kChaseHopAt = 4200;      // the pet jumps for the prop from here to the end
constexpr uint32_t kChaseHeartsMs = 800;
constexpr uint32_t kChaseStepMs = 50;
constexpr int32_t kChaseSpeedQ = 10;        // quarter units per step: 50 units per second
constexpr int32_t kChaseReachQ = 12 * 4;    // stops this close to the prop
constexpr int16_t kChaseStartX = -40;
constexpr int16_t kPropHeight = -34;

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
         a.symbol == b.symbol && a.symbolProgress == b.symbolProgress && a.prop == b.prop &&
         a.propKind == b.propKind && a.propX == b.propX && a.propY == b.propY;
}

uint32_t nextRandom(PpStage& st)
{
  uint32_t x = st.rng;
  if (x == 0) {
    x = 0x9E3779B9u;
  }
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  st.rng = x;
  return x;
}

void startStand(PpStage& st, int16_t x, uint32_t at, uint32_t dur)
{
  st.wanderKind = kWanderStand;
  st.wanderAt = at;
  st.wanderDur = dur;
  st.wanderFromX = x;
  st.wanderToX = x;
}

void startWalk(PpStage& st, int16_t x, uint32_t at)
{
  const int32_t span = PP_STAGE_WALK_SPAN;
  int32_t target = (int32_t)(nextRandom(st) % (uint32_t)(2 * span + 1)) - span;
  int32_t dist = target - x;
  if (dist < kWanderMinStep && dist > -kWanderMinStep) {
    target = (x > 0) ? x - 2 * kWanderMinStep : x + 2 * kWanderMinStep;
  }
  if (target > span) {
    target = span;
  }
  if (target < -span) {
    target = -span;
  }
  dist = target - x;
  if (dist < 0) {
    dist = -dist;
  }
  st.wanderKind = kWanderWalk;
  st.wanderAt = at;
  st.wanderDur = (uint32_t)(dist * 1000 / kWanderSpeed);
  if (st.wanderDur == 0) {
    st.wanderDur = 1;
  }
  st.wanderFromX = x;
  st.wanderToX = (int16_t)target;
}

// Moves on to later segments until the current one contains `now`.
void advanceWander(PpStage& st, const PpStageTimings& timings, uint32_t now)
{
  for (uint8_t guard = 0; guard < 32; ++guard) {
    if (elapsedSince(now, st.wanderAt) < st.wanderDur) {
      return;
    }
    const uint32_t at = st.wanderAt + st.wanderDur;
    const int16_t x = st.wanderToX;
    if (st.wanderKind == kWanderWalk) {
      startStand(st, x, at, kWanderStandMinMs + nextRandom(st) % kWanderStandSpreadMs);
      continue;
    }
    const uint32_t roll = nextRandom(st) % 10;
    if (roll < 5) {
      startWalk(st, x, at);
    } else if (roll < 8 && loopTotal(timings.anim[PP_SANIM_IDLE]) > 0) {
      startStand(st, x, at, loopTotal(timings.anim[PP_SANIM_IDLE]));
      st.wanderKind = kWanderBob;
    } else {
      startStand(st, x, at, kWanderStandMinMs + nextRandom(st) % kWanderStandSpreadMs);
    }
  }
  // A long gap (the screen was dark): do not replay it, just stand here from now.
  startStand(st, st.wanderToX, now, kWanderStandMinMs);
}

int16_t wanderX(const PpStage& st, uint32_t now)
{
  if (st.wanderKind != kWanderWalk || st.wanderDur == 0) {
    return st.wanderToX;
  }
  uint32_t e = elapsedSince(now, st.wanderAt);
  if (e >= st.wanderDur) {
    return st.wanderToX;
  }
  const int32_t from = st.wanderFromX;
  const int32_t to = st.wanderToX;
  return (int16_t)(from + (to - from) * (int32_t)e / (int32_t)st.wanderDur);
}

bool wandering(const PpStage& st)
{
  return st.base == PP_STAGE_IDLE && st.animationsOn && st.moment == PP_MOMENT_NONE;
}

int16_t chasePropX(uint32_t e)
{
  if (e < 1200) {
    return 55;
  }
  if (e < 2000) {
    return (int16_t)(55 - (int32_t)(110 * (e - 1200)) / 800);
  }
  if (e < 3000) {
    return -55;
  }
  if (e < 3600) {
    return (int16_t)(-55 + (int32_t)(70 * (e - 3000)) / 600);
  }
  return 15;
}

int16_t chasePropY(uint32_t e)
{
  const uint32_t ph = e % 600;
  const int32_t tri = (ph < 300) ? (int32_t)ph : (int32_t)(600 - ph);   // 0..300
  return (int16_t)(kPropHeight + tri * 10 / 300 - 5);
}

// The pet runs after the prop. Worked out by stepping from the start, so any
// moment of a chase always looks the same.
void chasePose(const PpStageTimings& timings, uint32_t e, PpStagePose& p)
{
  const uint32_t run = (e < kChaseHopAt) ? e : kChaseHopAt;
  int32_t xq = kChaseStartX * 4;
  int8_t dir = 1;
  bool moving = false;
  for (uint32_t t = 0; t < run; t += kChaseStepMs) {
    const int32_t diff = chasePropX(t) * 4 - xq;
    if (diff > kChaseReachQ) {
      xq += kChaseSpeedQ;
      dir = 1;
      moving = true;
    } else if (diff < -kChaseReachQ) {
      xq -= kChaseSpeedQ;
      dir = -1;
      moving = true;
    } else {
      moving = false;
    }
  }
  p.dx = (int16_t)(xq / 4);
  p.prop = true;
  p.propX = chasePropX(e);
  p.propY = chasePropY(e);
  if (e >= kChaseHopAt) {
    resolveLoop(timings, PP_SANIM_HOP, e - kChaseHopAt, p);
    p.mirror = p.propX < p.dx;
    const uint32_t h = e - kChaseHopAt;
    p.symbol = PP_SYM_HEARTS;
    p.symbolProgress = (uint16_t)(h >= kChaseHeartsMs ? 1000 : (h * 1000u) / kChaseHeartsMs);
  } else if (moving) {
    resolveLoop(timings, PP_SANIM_WALK, e, p);
    p.mirror = dir < 0;
  } else {
    p.mirror = p.propX < p.dx;
  }
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
  st.rng = (now * 2654435761u) ^ 0x9E3779B9u;
  if (st.rng == 0) {
    st.rng = 0x9E3779B9u;
  }
  startStand(st, 0, now, kWanderFirstStandMs);
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
  startStand(st, 0, now, kWanderFirstStandMs);
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
  // The pet reacts where it stands, and stays there a moment afterwards.
  startStand(st, wanderX(st, now), now, ppStageReactionMs(reaction) + kReactionSettleMs);
  st.reaction = reaction;
  st.reactionAt = now;
  st.hasLast = false;
}

void ppStageNudge(PpStage& st, uint32_t now)
{
  if (!wandering(st) || reactionActive(st, now)) {
    return;
  }
  startWalk(st, wanderX(st, now), now);
  st.hasLast = false;
}

PpStageProp ppStagePropFor(PpStageScene scene)
{
  switch (scene) {
    case PP_SCENE_BEACH: {
      return PP_PROP_BALL;
    }
    case PP_SCENE_FOREST: {
      return PP_PROP_LEAF;
    }
    case PP_SCENE_CAMP_NIGHT: {
      return PP_PROP_FIREFLY;
    }
    default: {
      return PP_PROP_BUTTERFLY;
    }
  }
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
  if (wandering(st)) {
    advanceWander(st, timings, now);
  }

  if (st.base == PP_STAGE_EMPTY) {
    p.scene = PP_SCENE_MEADOW;
    p.emptyBall = true;
  } else if (st.moment == PP_MOMENT_PLAY) {
    uint32_t e = elapsedSince(now, st.momentAt);
    if (e > PP_STAGE_PLAY_MS) {
      e = PP_STAGE_PLAY_MS;
    }
    p.scene = st.momentScene;
    p.propKind = (uint8_t)ppStagePropFor(st.momentScene);
    chasePose(timings, e, p);
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
    if (st.base == PP_STAGE_IDLE) {
      p.dx = wanderX(st, now);
    }
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
    // Idle: the wander. Standing is the still first frame; a bob is one pass of
    // the idle animation; a walk moves to a new spot facing the way it goes.
    const uint32_t e = elapsedSince(now, st.wanderAt);
    p.dx = wanderX(st, now);
    if (st.wanderKind == kWanderWalk) {
      resolveLoop(timings, PP_SANIM_WALK, e, p);
      p.mirror = st.wanderToX < st.wanderFromX;
    } else if (st.wanderKind == kWanderBob) {
      resolveLoop(timings, PP_SANIM_IDLE, e, p);
    }
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
