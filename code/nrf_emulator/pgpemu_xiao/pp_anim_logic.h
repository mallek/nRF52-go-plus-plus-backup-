#pragma once

#include <stdint.h>

#include "pp_feedback_logic.h"

// Pure animation timing: no Arduino, no hardware. Compiled into the sketch and
// also by the host tests in tests/anim/.

static constexpr uint32_t kAnimDurationMs = 2000;

enum PpAnimKind : uint8_t {
  PP_ANIM_NONE = 0,
  PP_ANIM_CATCH,
  PP_ANIM_SPIN
};

struct PpAnimState {
  PpAnimKind kind;
  uint32_t startedAt;
};

// Starts (or restarts, replacing whatever is playing) an animation.
void ppAnimStart(PpAnimState& state, PpAnimKind kind, uint32_t now);

// 0..1000 through the animation, clamped at 1000. Safe across millis() wrap.
uint16_t ppAnimProgress(const PpAnimState& state, uint32_t now);

// True when nothing is playing or the duration has elapsed.
bool ppAnimDone(const PpAnimState& state, uint32_t now);

PpAnimKind ppAnimKindForEvent(PpFeedbackEvent event);
