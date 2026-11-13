#pragma once

#include <stdint.h>

#include "pp_feedback_logic.h"

// Pure animation timing: no Arduino, no hardware. Compiled into the sketch and
// also by the host tests in tests/anim/.

static constexpr uint32_t kAnimDurationMs = 2000;
static constexpr uint32_t kEggDurationMs = 2500;
// Easter egg: this many B taps inside the window.
static constexpr uint8_t kEggTaps = 5;
static constexpr uint32_t kEggTapWindowMs = 2000;

enum PpAnimKind : uint8_t {
  PP_ANIM_NONE = 0,
  PP_ANIM_CATCH,
  PP_ANIM_SPIN,
  PP_ANIM_EGG,
  PP_ANIM_READY
};

struct PpAnimState {
  PpAnimKind kind;
  uint32_t startedAt;
};

struct PpTapCounter {
  uint8_t count;
  uint32_t firstAt;
};

uint32_t ppAnimDurationMs(PpAnimKind kind);

// Starts (or restarts, replacing whatever is playing) an animation.
void ppAnimStart(PpAnimState& state, PpAnimKind kind, uint32_t now);

// 0..1000 through the animation, clamped at 1000. Safe across millis() wrap.
uint16_t ppAnimProgress(const PpAnimState& state, uint32_t now);

// True when nothing is playing or the duration has elapsed.
bool ppAnimDone(const PpAnimState& state, uint32_t now);

PpAnimKind ppAnimKindForEvent(PpFeedbackEvent event);

// Records a tap; true (and resets) on the kEggTaps-th tap inside kEggTapWindowMs
// of the first. A tap outside the window starts a new window.
bool ppTapCounterTap(PpTapCounter& counter, uint32_t now);
