#include "pp_anim_logic.h"

void ppAnimStart(PpAnimState& state, PpAnimKind kind, uint32_t now)
{
  state.kind = kind;
  state.startedAt = now;
}

uint16_t ppAnimProgress(const PpAnimState& state, uint32_t now)
{
  uint32_t elapsed = now - state.startedAt;
  if (elapsed >= kAnimDurationMs) {
    return 1000;
  }
  return static_cast<uint16_t>((elapsed * 1000u) / kAnimDurationMs);
}

bool ppAnimDone(const PpAnimState& state, uint32_t now)
{
  if (state.kind == PP_ANIM_NONE) {
    return true;
  }
  return (now - state.startedAt) >= kAnimDurationMs;
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
