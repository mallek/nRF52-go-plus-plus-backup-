#pragma once

#include <stdint.h>

struct PpBatteryState {
  uint16_t millivolts;
  uint8_t percent;
  bool charging;    // BQ25100 ~CHG low: actively charging
  bool usbPresent;  // VBUS detected
};

void ppFeedbackBatteryBegin();

// Samples at most every couple of seconds; cheap to call every loop tick.
// Returns true when a new sample was taken.
bool ppFeedbackBatteryUpdate(uint32_t nowMs);

const PpBatteryState& ppFeedbackBatteryState();
