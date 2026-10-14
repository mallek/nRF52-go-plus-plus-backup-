#pragma once

#include <stdint.h>

// On-device field log of LED/effect writes, saved to InternalFS (/fblog.bin, capped at
// 8 KB). Serial commands while plugged in: "dump", "clear", "flush".
void ppFeedbackLogBegin();

// Safe to call from a BLE callback: copies into a small RAM queue only.
void ppFeedbackLogPayload(const uint8_t* data, uint16_t len);

// Loop-side work: drain the queue into the store, flush to flash periodically,
// handle serial commands.
void ppFeedbackLogMarker(uint8_t kind);
// One battery sample (millivolts, USB present). Loop-side only.
void ppFeedbackLogBattery(uint16_t millivolts, bool usbPresent);
void ppFeedbackLogUpdate(uint32_t nowMs);

// Write anything pending (before System OFF).
void ppFeedbackLogFlush();
