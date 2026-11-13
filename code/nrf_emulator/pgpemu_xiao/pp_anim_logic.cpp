#include "pp_anim_logic.h"

uint32_t ppAnimDurationMs(PpAnimKind kind)
{
  return kind == PP_ANIM_EGG ? kEggDurationMs : kAnimDurationMs;
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
    default: {
      return PP_ANIM_NONE;
    }
  }
}

bool ppTapCounterTap(PpTapCounter& counter, uint32_t now)
{
  if (counter.count == 0 || (now - counter.firstAt) > kEggTapWindowMs) {
    counter.count = 1;
    counter.firstAt = now;
    return false;
  }
  ++counter.count;
  if (counter.count >= kEggTaps) {
    counter.count = 0;
    return true;
  }
  return false;
}
