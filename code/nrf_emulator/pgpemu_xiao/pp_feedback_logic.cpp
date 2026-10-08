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

// Effect families seen from the app, keyed by entry count (byte 3).
static constexpr uint8_t kCountPokemonPrompt = 0x0f;  // 49-byte write
static constexpr uint8_t kCountPokestopPrompt = 0x1b; // 85-byte write
static constexpr uint8_t kCountActionAck = 0x01;      // 7-byte "00 00 00 01 0a 00 00"

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

void ppBatteryGaugeFrame(uint8_t percent, bool charging, bool blinkOn, bool lit[PP_GAUGE_SEGMENTS])
{
  if (percent > 100) {
    percent = 100;
  }
  uint8_t full = static_cast<uint8_t>((static_cast<uint16_t>(percent) * PP_GAUGE_SEGMENTS) / 100);
  for (uint8_t i = 0; i < PP_GAUGE_SEGMENTS; ++i) {
    if (i < full) {
      lit[i] = true;
    } else if (charging && i == full) {
      lit[i] = blinkOn;
    } else {
      lit[i] = false;
    }
  }
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
  if (count == kCountPokemonPrompt) {
    return PP_FEEDBACK_POKEMON_NEARBY;
  }
  if (count == kCountPokestopPrompt) {
    return PP_FEEDBACK_POKESTOP_NEARBY;
  }
  if (count == kCountActionAck) {
    return PP_FEEDBACK_NONE;
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
      return "result-unknown";
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
  }
  return "?";
}
