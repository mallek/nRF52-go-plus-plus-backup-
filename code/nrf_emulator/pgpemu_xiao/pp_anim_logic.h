#pragma once

#include <stdint.h>

#include "pp_feedback_logic.h"

// Pure animation timing: no Arduino, no hardware. Compiled into the sketch and
// also by the host tests in tests/anim/.

static constexpr uint32_t kAnimDurationMs = 2000;
static constexpr uint32_t kReadyDurationMs = 2500;
static constexpr uint32_t kResetDurationMs = 3000;
// Quick taps on B: this many inside the window.
static constexpr uint8_t kQuickTaps = 5;
static constexpr uint32_t kQuickTapWindowMs = 2000;

enum PpAnimKind : uint8_t {
  PP_ANIM_NONE = 0,
  PP_ANIM_CATCH,
  PP_ANIM_SPIN,
  PP_ANIM_READY,
  PP_ANIM_BOOT,
  PP_ANIM_RESET
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

// Whether an event may light a dark screen. Catches, flees and spins do not:
// they only count, and you tap a button to look. The handshake finishing and
// the warnings (box full, no balls) do: they want action in the game, and
// lighting the screen lets the decode be checked against the app. A failed
// spin does not.
// Idea from Jackv-makes/nRF52-PoGoPP-Emu (AGPL), which keeps its screen dark
// on catch and spin for battery life.
bool ppEventWakesScreen(PpFeedbackEvent event);

// What one feedback event does to the screen, given whether it is lit and the
// Animations setting. With animations off nothing plays and catches, spins and
// READY neither light nor extend the screen; warnings still light a dark one.
enum PpScreenActionKind : uint8_t { PP_SCREEN_NOTHING, PP_SCREEN_ANIM, PP_SCREEN_LIGHT_WARNING };

struct PpScreenAction {
  PpScreenActionKind kind;
  PpAnimKind anim;  // set for PP_SCREEN_ANIM, else PP_ANIM_NONE
};

PpScreenAction ppEventScreenAction(PpFeedbackEvent event, bool backlightOn, bool animationsOn);

// Records a tap; true (and resets) on the kQuickTaps-th tap inside kQuickTapWindowMs
// of the first. A tap outside the window starts a new window.
bool ppTapCounterTap(PpTapCounter& counter, uint32_t now);
