#pragma once

#include <stdint.h>

#include "pp_feedback_logic.h"

// Screen + settings menu (CLUE). On boards without a display every call is a no-op
// and ppUiOwnsButtons() is always false.
//
// CLUE controls:
//   hold A 5 s     factory reset (handled by the lifecycle code, not here)
//   hold A+B 1.5 s open the settings menu; B = next, A = select. Back is the
//                  first row of every list. It closes after 20 s without a press.
//   A or B         wake the screen for 30 s (5 s while disconnected)
// A catch, flee or spin does not light a dark screen (battery); it only counts.
// The animations play when the screen is already lit.
//   B x5 in 2 s    easter egg
void ppDisplayBegin();
void ppDisplayUpdate();
void ppDisplayOnEvent(PpFeedbackEvent event);
void ppDisplayOnFactoryReset();
// The disconnected System OFF timer, so the screen can show a countdown.
void ppDisplaySetSleepDeadline(bool armed, uint32_t atMs);
void ppDisplayShutdown();

// True while the menu or a two-button gesture is using the buttons, so the
// hold-A factory reset must not fire.
bool ppUiOwnsButtons();
