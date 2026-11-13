#pragma once

#include <stdint.h>

#include "pp_feedback_logic.h"

// Screen + identity menu (CLUE). On boards without a display every call is a no-op
// and ppUiOwnsButtons() is always false.
//
// CLUE controls:
//   hold A 5 s     factory reset (handled by the lifecycle code, not here)
//   hold A+B 1.5 s open the identity menu; B = next, A = select
//   B              wake the screen
//   B x5 in 2 s    easter egg
void ppDisplayBegin();
void ppDisplayUpdate();
void ppDisplayOnEvent(PpFeedbackEvent event);
void ppDisplayOnFactoryReset();
void ppDisplayShutdown();

// True while the menu or a two-button gesture is using the buttons, so the
// hold-A factory reset must not fire.
bool ppUiOwnsButtons();
