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
  PP_FEEDBACK_STORAGE_FULL,  // raw full signal; the source is not known from the bytes
  PP_FEEDBACK_POKEMON_FULL,  // derived: full signal that followed a Pokemon alert
  PP_FEEDBACK_BAG_FULL,      // derived: full signal that followed a Pokestop alert
  PP_FEEDBACK_STOP_PASSED,   // Pokestop left range without a spin (10 FF FF after in range)
  PP_FEEDBACK_UNKNOWN_SINGLE,
  PP_FEEDBACK_READY          // GO handshake finished (first pair or reconnect); appended to keep numbering
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

// Tracks whether the Pokemon box and/or the bag is full. The full signal is the same
// for both, so the event just before it says which one it was. A catch or a throw
// clears the Pokemon flag, a Pokestop spin clears the bag flag.
struct PpFullState {
  bool pokemonFull;
  bool bagFull;
  PpFeedbackEvent context;  // last Pokemon-nearby or Pokestop-nearby event
  uint32_t contextMs;
  PpFeedbackEvent lastFull; // what the latest full signal was: POKEMON_FULL, BAG_FULL or STORAGE_FULL
};

static constexpr uint32_t PP_FULL_CONTEXT_MS = 20000;

void ppFullStateInit(PpFullState& state);
// Feed every classified event in order. Returns true while anything is full.
bool ppFullStateNote(PpFullState& state, PpFeedbackEvent event, uint32_t nowMs);
