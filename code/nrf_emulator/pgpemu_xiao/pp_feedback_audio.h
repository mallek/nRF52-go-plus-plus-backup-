#pragma once

#include <stdint.h>

#include "pp_feedback_logic.h"

// MAX98357A over I2S: BCLK D8, LRC D9, DIN D10. Sounds are short tone sequences
// played without blocking; the I2S peripheral is stopped between sounds so the amp
// sleeps (it shuts down on clock loss) and draws nothing.
void ppFeedbackAudioBegin();
void ppFeedbackAudioPlayEvent(PpFeedbackEvent event, uint32_t nowMs);
void ppFeedbackAudioUpdate(uint32_t nowMs);
void ppFeedbackAudioStop();
