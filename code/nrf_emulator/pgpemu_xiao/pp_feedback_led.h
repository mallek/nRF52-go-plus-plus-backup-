#pragma once

#include <stdint.h>

#include "pp_feedback_logic.h"

// 7-LED WS2812B ring on D7 (pixel 0 = center, 1..6 = outer). Events take over the
// whole ring. Between events the center is a static status light (low battery, charging)
// and the outer six are dark.
void ppFeedbackLedBegin();
void ppFeedbackLedSetBattery(uint8_t percent, bool charging, bool usbPresent);
// Outer six breathe between events while the Pokemon box (amber) or the bag (cyan) is
// full. If both are set (source unknown) they alternate.
void ppFeedbackLedSetFull(bool pokemonFull, bool bagFull);
void ppFeedbackLedPlayEvent(PpFeedbackEvent event, uint32_t nowMs);
void ppFeedbackLedUpdate(uint32_t nowMs);
void ppFeedbackLedOff();
