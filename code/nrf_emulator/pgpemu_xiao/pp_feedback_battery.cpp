#include <Arduino.h>
#include <nrf_soc.h>

#include "pp_board.h"
#include "pp_feedback_battery.h"
#include "pp_feedback_logic.h"

#if PP_HAS_BATTERY_GAUGE

namespace {

// XIAO nRF52840 battery wiring (variant.cpp):
//   P0.31 (PIN_VBAT)    - battery through a 1M / 510k divider
//   P0.14 (VBAT_ENABLE) - divider low side; LOW enables reading
//   P0.17 (D23)         - BQ25100 ~CHG, low while charging
// Seeed warns that P0.14 HIGH while charging can push P0.31 above its rating, and the
// variant init drives it HIGH. We drive it LOW once and leave it there (divider costs ~3 uA).
static constexpr uint8_t kPinChargeStatus = 23;
static constexpr uint32_t kSampleIntervalMs = 2000;
static constexpr uint8_t kSamplesPerReading = 8;
static constexpr float kDividerRatio = 1510.0f / 510.0f;
static constexpr float kAdcFullScaleMv = 3600.0f;  // 0.6 V reference, 1/6 gain
static constexpr float kAdcMaxCount = 4095.0f;     // 12-bit

static PpBatteryState state = { 0, 0, false, false };
static uint32_t lastSampleMs = 0;
static bool sampledOnce = false;

static uint16_t readMillivolts()
{
  uint32_t sum = 0;
  for (uint8_t i = 0; i < kSamplesPerReading; ++i) {
    sum += analogRead(PIN_VBAT);
  }
  float raw = static_cast<float>(sum) / kSamplesPerReading;
  return static_cast<uint16_t>(raw * kAdcFullScaleMv / kAdcMaxCount * kDividerRatio);
}

static bool readUsbPresent()
{
  uint32_t status = 0;
  if (sd_power_usbregstatus_get(&status) != NRF_SUCCESS) {
    return false;
  }
  return (status & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
}

}  // namespace

void ppFeedbackBatteryBegin()
{
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW);
  pinMode(kPinChargeStatus, INPUT_PULLUP);
  analogReference(AR_INTERNAL);
  analogReadResolution(12);
}

bool ppFeedbackBatteryUpdate(uint32_t nowMs)
{
  if (sampledOnce && nowMs - lastSampleMs < kSampleIntervalMs) {
    return false;
  }
  lastSampleMs = nowMs;
  sampledOnce = true;

  uint16_t mv = readMillivolts();
  // Light smoothing so the gauge does not flicker between segments.
  if (state.millivolts == 0) {
    state.millivolts = mv;
  } else {
    state.millivolts = static_cast<uint16_t>((state.millivolts * 3u + mv) / 4u);
  }
  state.percent = ppBatteryPercentFromMillivolts(state.millivolts);
  state.charging = digitalRead(kPinChargeStatus) == LOW;
  state.usbPresent = readUsbPresent();
  return true;
}

const PpBatteryState& ppFeedbackBatteryState()
{
  return state;
}

#else

// No battery gauge on this board (CLUE runs on USB power): never reports a sample,
// so the status light stays dark and no fully-charged event fires.
namespace {
static PpBatteryState state = { 0, 100, false, true };
}  // namespace

void ppFeedbackBatteryBegin() {}

bool ppFeedbackBatteryUpdate(uint32_t)
{
  return false;
}

const PpBatteryState& ppFeedbackBatteryState()
{
  return state;
}

#endif
