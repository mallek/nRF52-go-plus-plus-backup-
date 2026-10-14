#pragma once

#include <stdint.h>

// Pure feedback logic: no Arduino, no hardware. Compiled into the sketch and
// also by the host tests in tests/feedback/.

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
  PP_FEEDBACK_BAG_FULL,
  PP_FEEDBACK_UNKNOWN_SINGLE
};

// Battery percent from cell voltage, interpolated on a typical LiPo discharge curve.
uint8_t ppBatteryPercentFromMillivolts(uint16_t millivolts);

// What the static status light shows when no event is playing.
enum PpStatusLight : uint8_t {
  PP_STATUS_OFF = 0,
  PP_STATUS_AMBER,
  PP_STATUS_RED,
  PP_STATUS_CHARGING
};

static constexpr uint8_t PP_STATUS_WARN_PERCENT = 30;
static constexpr uint8_t PP_STATUS_LOW_PERCENT = 10;

// Dark above 30%. Amber below 30%, red below 10%. On USB while charging: CHARGING
// (takes priority, the caller pulses it). The CHG line without USB is ignored.
PpStatusLight ppStatusLight(uint8_t percent, bool charging, bool usbPresent);

// LED/effect writes from the app look like: 00 00 00 <count> then count x 3-byte entries,
// so len == 4 + 3 * count. The entries (not the count or length) identify the family:
// see the field-log payloads in tests/feedback.
PpFeedbackEvent ppClassifyLedWrite(const uint8_t* data, uint16_t len);

const char* ppFeedbackEventName(PpFeedbackEvent event);
