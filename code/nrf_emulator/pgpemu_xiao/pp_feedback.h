#pragma once

#include <stdint.h>

#include "pp_feedback_logic.h"

// Light + sound feedback for the autocatcher prototype.
//
// BLE callbacks only POST events (cheap, non-blocking). All animation, audio and
// battery sampling runs from ppFeedbackUpdate() in loop(), so the BLE protocol timing
// is never delayed by feedback work.
void ppFeedbackBegin();
void ppFeedbackUpdate();

void ppFeedbackPost(PpFeedbackEvent event);

// Called from the LED/effect characteristic write. Logs the full payload (the effect
// patterns are not secret) and posts the classified event.
void ppFeedbackOnLedWrite(const uint8_t* data, uint16_t len);

// Dark ring + silent amp before System OFF; NeoPixels would otherwise hold their colors.
void ppFeedbackShutdown();
