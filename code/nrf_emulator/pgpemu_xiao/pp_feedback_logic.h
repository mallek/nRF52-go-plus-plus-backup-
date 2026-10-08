#pragma once

#include <stdint.h>

// Pure feedback logic: no Arduino, no hardware. Compiled into the sketch and
// also by the host tests in tests/feedback/.

static constexpr uint8_t PP_GAUGE_SEGMENTS = 6;

enum PpFeedbackEvent : uint8_t {
  PP_FEEDBACK_NONE = 0,
  PP_FEEDBACK_POKEMON_NEARBY,
  PP_FEEDBACK_POKESTOP_NEARBY,
  PP_FEEDBACK_RESULT_UNKNOWN,
  PP_FEEDBACK_CAUGHT,
  PP_FEEDBACK_FLED,
  PP_FEEDBACK_POKESTOP_SPUN,
  PP_FEEDBACK_CONNECTED,
  PP_FEEDBACK_DISCONNECTED,
  PP_FEEDBACK_FULLY_CHARGED,
  PP_FEEDBACK_BAG_FULL
};

// Battery percent from cell voltage, interpolated on a typical LiPo discharge curve.
uint8_t ppBatteryPercentFromMillivolts(uint16_t millivolts);

// Which of the 6 outer LEDs are lit. Each segment is 1/6 of the charge.
// Not charging: completed segments solid. Charging: completed segments solid and the
// segment currently filling follows blinkOn.
void ppBatteryGaugeFrame(uint8_t percent, bool charging, bool blinkOn, bool lit[PP_GAUGE_SEGMENTS]);

// LED/effect writes from the app look like: 00 00 00 <count> then count x 3-byte entries,
// so len == 4 + 3 * count. The entries (not the count or length) identify the family:
// see the field-log payloads in tests/feedback.
PpFeedbackEvent ppClassifyLedWrite(const uint8_t* data, uint16_t len);

const char* ppFeedbackEventName(PpFeedbackEvent event);
