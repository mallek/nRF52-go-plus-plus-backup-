#pragma once

#include <stdint.h>

#include "pp_feedback_logic.h"

// 7-LED WS2812B ring on D0: one center pixel for events, six outer pixels for the
// battery gauge.
void ppFeedbackLedBegin();
void ppFeedbackLedSetGauge(uint8_t percent, bool charging);
void ppFeedbackLedPlayEvent(PpFeedbackEvent event, uint32_t nowMs);
void ppFeedbackLedUpdate(uint32_t nowMs);
void ppFeedbackLedOff();
