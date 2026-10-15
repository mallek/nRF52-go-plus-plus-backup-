#include "pp_feedback_logic.h"

namespace {

struct VoltagePoint {
  uint16_t millivolts;
  uint8_t percent;
};

// Resting-voltage curve for a single LiPo cell, high to low.
static const VoltagePoint kLipoCurve[] = {
  { 4200, 100 }, { 4150, 95 }, { 4110, 90 }, { 4080, 85 }, { 4020, 80 },
  { 3980, 75 },  { 3950, 70 }, { 3910, 65 }, { 3870, 60 }, { 3850, 55 },
  { 3840, 50 },  { 3820, 45 }, { 3800, 40 }, { 3790, 35 }, { 3770, 30 },
  { 3750, 25 },  { 3730, 20 }, { 3710, 15 }, { 3690, 10 }, { 3610, 5 },
  { 3270, 0 }
};
static constexpr uint8_t kLipoCurveLen = sizeof(kLipoCurve) / sizeof(kLipoCurve[0]);

// Effect writes are a list of 3-byte entries. Families were identified from the
// 2026-10-08 field log, labeled against the in-game Journal (see tests/feedback).
struct Entry {
  uint8_t a;
  uint8_t b;
  uint8_t c;
};

static const Entry kShake = { 0x03, 0x88, 0xF8 };         // opens every encounter result
static const Entry kRainbowStart = { 0x03, 0x08, 0xF0 };  // caught: 3-color cycle after the shake
static const Entry kRedBlink = { 0x01, 0x0F, 0xF0 };      // fled: blinks after the shake
static const Entry kCycleStart = { 0x03, 0xF0, 0x80 };    // Pokestop items: 3-color cycle only
static const Entry kStopNearby = { 0x04, 0x00, 0xFF };    // Pokestop in range
static const Entry kEncounter = { 0x02, 0xCC, 0xF0 };     // Pokemon encounter starting
static const Entry kEncounterAlt = { 0x10, 0xF0, 0xF0 };  // also an encounter: always ack + result after
static const Entry kStorageFull = { 0x18, 0x0F, 0xF0 };   // single long flash: box or bag full
static const Entry kStopPassed = { 0x10, 0xFF, 0xFF };    // after Pokestop in range, 135 of 244 on a drive

static bool entryIs(const uint8_t* data, uint8_t index, const Entry& e)
{
  const uint8_t* p = data + 4 + 3 * index;
  return p[0] == e.a && p[1] == e.b && p[2] == e.c;
}

static bool containsEntry(const uint8_t* data, uint8_t count, const Entry& e)
{
  for (uint8_t i = 0; i < count; ++i) {
    if (entryIs(data, i, e)) {
      return true;
    }
  }
  return false;
}

}  // namespace

uint8_t ppBatteryPercentFromMillivolts(uint16_t millivolts)
{
  if (millivolts >= kLipoCurve[0].millivolts) {
    return 100;
  }
  if (millivolts <= kLipoCurve[kLipoCurveLen - 1].millivolts) {
    return 0;
  }
  for (uint8_t i = 1; i < kLipoCurveLen; ++i) {
    const VoltagePoint& hi = kLipoCurve[i - 1];
    const VoltagePoint& lo = kLipoCurve[i];
    if (millivolts >= lo.millivolts) {
      uint32_t span = hi.millivolts - lo.millivolts;
      uint32_t above = millivolts - lo.millivolts;
      uint32_t percentSpan = hi.percent - lo.percent;
      return static_cast<uint8_t>(lo.percent + (above * percentSpan + span / 2) / span);
    }
  }
  return 0;
}

PpStatusLight ppStatusLight(uint8_t percent, bool charging, bool usbPresent)
{
  if (usbPresent && charging) {
    return PP_STATUS_CHARGING;
  }
  if (percent < PP_STATUS_LOW_PERCENT) {
    return PP_STATUS_RED;
  }
  if (percent < PP_STATUS_WARN_PERCENT) {
    return PP_STATUS_AMBER;
  }
  return PP_STATUS_OFF;
}

PpFeedbackEvent ppClassifyLedWrite(const uint8_t* data, uint16_t len)
{
  if (data == nullptr || len < 4) {
    return PP_FEEDBACK_NONE;
  }
  if (data[0] != 0x00 || data[1] != 0x00 || data[2] != 0x00) {
    return PP_FEEDBACK_NONE;
  }
  uint8_t count = data[3];
  if (len != 4 + 3 * static_cast<uint16_t>(count)) {
    return PP_FEEDBACK_NONE;
  }
  if (count == 0) {
    return PP_FEEDBACK_NONE;
  }
  if (count == 1) {
    // A dark single entry (color bytes 00 00) is an action ack. 18 0F F0 is the long
    // amber flash shown when the Pokemon box or the bag is full. 10 FF FF follows
    // Pokestop in range and is most likely the stop leaving range (drive-by). Any
    // other colored single entry is not decoded yet.
    if (data[5] == 0x00 && data[6] == 0x00) {
      return PP_FEEDBACK_NONE;
    }
    if (entryIs(data, 0, kStorageFull)) {
      return PP_FEEDBACK_STORAGE_FULL;
    }
    if (entryIs(data, 0, kStopPassed)) {
      return PP_FEEDBACK_STOP_PASSED;
    }
    return PP_FEEDBACK_UNKNOWN_SINGLE;
  }
  if (entryIs(data, 0, kShake)) {
    if (containsEntry(data, count, kRainbowStart)) {
      return PP_FEEDBACK_CAUGHT;
    }
    if (containsEntry(data, count, kRedBlink)) {
      return PP_FEEDBACK_FLED;
    }
    return PP_FEEDBACK_RESULT_UNKNOWN;
  }
  if (entryIs(data, 0, kCycleStart)) {
    return PP_FEEDBACK_POKESTOP_SPUN;
  }
  if (entryIs(data, 0, kStopNearby)) {
    return PP_FEEDBACK_POKESTOP_NEARBY;
  }
  if (entryIs(data, 0, kEncounter) || entryIs(data, 0, kEncounterAlt)) {
    return PP_FEEDBACK_POKEMON_NEARBY;
  }
  return PP_FEEDBACK_RESULT_UNKNOWN;
}

const char* ppFeedbackEventName(PpFeedbackEvent event)
{
  switch (event) {
    case PP_FEEDBACK_NONE: {
      return "none";
    }
    case PP_FEEDBACK_POKEMON_NEARBY: {
      return "pokemon-nearby";
    }
    case PP_FEEDBACK_POKESTOP_NEARBY: {
      return "pokestop-nearby";
    }
    case PP_FEEDBACK_RESULT_UNKNOWN: {
      return "unknown";
    }
    case PP_FEEDBACK_CAUGHT: {
      return "caught";
    }
    case PP_FEEDBACK_FLED: {
      return "fled";
    }
    case PP_FEEDBACK_POKESTOP_SPUN: {
      return "pokestop-spun";
    }
    case PP_FEEDBACK_CONNECTED: {
      return "connected";
    }
    case PP_FEEDBACK_DISCONNECTED: {
      return "disconnected";
    }
    case PP_FEEDBACK_FULLY_CHARGED: {
      return "fully-charged";
    }
    case PP_FEEDBACK_STORAGE_FULL: {
      return "storage-full";
    }
    case PP_FEEDBACK_POKEMON_FULL: {
      return "pokemon-full";
    }
    case PP_FEEDBACK_BAG_FULL: {
      return "bag-full";
    }
    case PP_FEEDBACK_STOP_PASSED: {
      return "stop-passed";
    }
    case PP_FEEDBACK_UNKNOWN_SINGLE: {
      return "unknown-single";
    }
  }
  return "?";
}

void ppFullStateInit(PpFullState& state)
{
  state.pokemonFull = false;
  state.bagFull = false;
  state.context = PP_FEEDBACK_NONE;
  state.contextMs = 0;
  state.lastFull = PP_FEEDBACK_NONE;
}

bool ppFullStateNote(PpFullState& state, PpFeedbackEvent event, uint32_t nowMs)
{
  switch (event) {
    case PP_FEEDBACK_POKEMON_NEARBY:
    case PP_FEEDBACK_POKESTOP_NEARBY: {
      state.context = event;
      state.contextMs = nowMs;
      break;
    }
    case PP_FEEDBACK_STORAGE_FULL: {
      bool fresh = state.context != PP_FEEDBACK_NONE && nowMs - state.contextMs <= PP_FULL_CONTEXT_MS;
      if (fresh && state.context == PP_FEEDBACK_POKEMON_NEARBY) {
        state.pokemonFull = true;
        state.lastFull = PP_FEEDBACK_POKEMON_FULL;
      } else if (fresh && state.context == PP_FEEDBACK_POKESTOP_NEARBY) {
        state.bagFull = true;
        state.lastFull = PP_FEEDBACK_BAG_FULL;
      } else {
        state.pokemonFull = true;
        state.bagFull = true;
        state.lastFull = PP_FEEDBACK_STORAGE_FULL;
      }
      break;
    }
    case PP_FEEDBACK_CAUGHT:
    case PP_FEEDBACK_FLED: {
      state.pokemonFull = false;
      break;
    }
    case PP_FEEDBACK_POKESTOP_SPUN: {
      state.bagFull = false;
      break;
    }
    default: {
      break;
    }
  }
  return state.pokemonFull || state.bagFull;
}
