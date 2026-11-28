#include "pp_anim_logic.h"

uint32_t ppAnimDurationMs(PpAnimKind kind)
{
  if (kind == PP_ANIM_RESET) {
    return kResetDurationMs;
  }
  return kind == PP_ANIM_READY ? kReadyDurationMs : kAnimDurationMs;
}

void ppAnimStart(PpAnimState& state, PpAnimKind kind, uint32_t now)
{
  state.kind = kind;
  state.startedAt = now;
}

uint16_t ppAnimProgress(const PpAnimState& state, uint32_t now)
{
  uint32_t elapsed = now - state.startedAt;
  uint32_t duration = ppAnimDurationMs(state.kind);
  if (elapsed >= duration) {
    return 1000;
  }
  return static_cast<uint16_t>((elapsed * 1000u) / duration);
}

bool ppAnimDone(const PpAnimState& state, uint32_t now)
{
  if (state.kind == PP_ANIM_NONE) {
    return true;
  }
  return (now - state.startedAt) >= ppAnimDurationMs(state.kind);
}

PpAnimKind ppAnimKindForEvent(PpFeedbackEvent event)
{
  switch (event) {
    case PP_FEEDBACK_CAUGHT: {
      return PP_ANIM_CATCH;
    }
    case PP_FEEDBACK_POKESTOP_SPUN: {
      return PP_ANIM_SPIN;
    }
    case PP_FEEDBACK_READY: {
      return PP_ANIM_READY;
    }
    default: {
      return PP_ANIM_NONE;
    }
  }
}

bool ppEventWakesScreen(PpFeedbackEvent event)
{
  switch (event) {
    case PP_FEEDBACK_READY:
    case PP_FEEDBACK_POKEMON_FULL:
    case PP_FEEDBACK_BAG_FULL:
    case PP_FEEDBACK_STORAGE_FULL:
    case PP_FEEDBACK_BALLS_EMPTY: {
      return true;
    }
    default: {
      return false;
    }
  }
}

bool ppTapCounterTap(PpTapCounter& counter, uint32_t now)
{
  if (counter.count == 0 || (now - counter.firstAt) > kQuickTapWindowMs) {
    counter.count = 1;
    counter.firstAt = now;
    return false;
  }
  ++counter.count;
  if (counter.count >= kQuickTaps) {
    counter.count = 0;
    return true;
  }
  return false;
}

static PpScreenAction makeAction(PpScreenActionKind kind, PpAnimKind anim, PpPetReaction reaction, bool lightScreen)
{
  PpScreenAction a = { kind, anim, reaction, lightScreen };
  return a;
}

PpScreenAction ppEventScreenAction(PpFeedbackEvent event, bool backlightOn, bool animationsOn)
{
  PpScreenAction nothing = makeAction(PP_SCREEN_NOTHING, PP_ANIM_NONE, PP_REACT_NONE, false);
  PpAnimKind kind = ppAnimKindForEvent(event);
  if (!animationsOn) {
    // Events that would animate are silent; the rest are warnings.
    if (kind == PP_ANIM_NONE && !backlightOn && ppEventWakesScreen(event)) {
      return makeAction(PP_SCREEN_LIGHT_WARNING, PP_ANIM_NONE, PP_REACT_NONE, true);
    }
    return nothing;
  }
  if (!backlightOn && !ppEventWakesScreen(event)) {
    // Dark screen: catches and spins only count.
    return nothing;
  }
  if (kind != PP_ANIM_NONE) {
    return makeAction(PP_SCREEN_ANIM, kind, PP_REACT_NONE, !backlightOn);
  }
  if (!backlightOn) {
    return makeAction(PP_SCREEN_LIGHT_WARNING, PP_ANIM_NONE, PP_REACT_NONE, true);
  }
  return nothing;
}

PpScreenAction ppEventScreenActionFor(PpFeedbackEvent event, const PpScreenContext& ctx)
{
  if (!ctx.gachiOn || !ctx.animationsOn) {
    return ppEventScreenAction(event, ctx.backlightOn, ctx.animationsOn);
  }
  PpScreenAction nothing = makeAction(PP_SCREEN_NOTHING, PP_ANIM_NONE, PP_REACT_NONE, false);
  PpPetReaction reaction = PP_REACT_NONE;
  switch (event) {
    case PP_FEEDBACK_CAUGHT: {
      reaction = PP_REACT_HAPPY;
      break;
    }
    case PP_FEEDBACK_FLED: {
      reaction = PP_REACT_FLOP;
      break;
    }
    case PP_FEEDBACK_POKESTOP_SPUN: {
      reaction = ctx.berryRefused ? PP_REACT_BERRY_FULL : PP_REACT_BERRY;
      break;
    }
    case PP_FEEDBACK_POKEMON_NEARBY: {
      reaction = PP_REACT_ALERT;
      break;
    }
    case PP_FEEDBACK_READY: {
      reaction = PP_REACT_WAKE;
      break;
    }
    default: {
      break;
    }
  }
  if (reaction != PP_REACT_NONE) {
    if (ctx.backlightOn) {
      if (ctx.petOnStage) {
        return makeAction(PP_SCREEN_PET_REACTION, PP_ANIM_NONE, reaction, false);
      }
      return nothing;
    }
    if (event == PP_FEEDBACK_READY) {
      if (ctx.petOnStage) {
        return makeAction(PP_SCREEN_PET_REACTION, PP_ANIM_NONE, reaction, true);
      }
      return makeAction(PP_SCREEN_LIGHT_HOME, PP_ANIM_NONE, PP_REACT_NONE, true);
    }
    return nothing;
  }
  if (!ctx.backlightOn && ppEventWakesScreen(event)) {
    return makeAction(PP_SCREEN_LIGHT_WARNING, PP_ANIM_NONE, PP_REACT_NONE, true);
  }
  return nothing;
}
