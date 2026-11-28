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

PpScreenAction ppEventScreenAction(PpFeedbackEvent event, bool backlightOn, bool animationsOn)
{
  PpScreenAction nothing = { PP_SCREEN_NOTHING, PP_ANIM_NONE };
  PpAnimKind kind = ppAnimKindForEvent(event);
  if (!animationsOn) {
    // Events that would animate are silent; the rest are warnings.
    if (kind == PP_ANIM_NONE && !backlightOn && ppEventWakesScreen(event)) {
      PpScreenAction light = { PP_SCREEN_LIGHT_WARNING, PP_ANIM_NONE };
      return light;
    }
    return nothing;
  }
  if (!backlightOn && !ppEventWakesScreen(event)) {
    // Dark screen: catches and spins only count.
    return nothing;
  }
  if (kind != PP_ANIM_NONE) {
    PpScreenAction play = { PP_SCREEN_ANIM, kind };
    return play;
  }
  if (!backlightOn) {
    PpScreenAction light = { PP_SCREEN_LIGHT_WARNING, PP_ANIM_NONE };
    return light;
  }
  return nothing;
}
