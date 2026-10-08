#include <Adafruit_NeoPixel.h>

#include "pp_feedback_led.h"

namespace {

static constexpr uint8_t kRingPin = D0;
static constexpr uint8_t kRingCount = 7;
static constexpr uint8_t kCenterIndex = 0;      // most 7-LED jewels put pixel 0 in the middle
static constexpr uint8_t kFirstOuterIndex = 1;  // outer pixels 1..6
static constexpr uint8_t kBrightness = 40;      // 7 px at full white would be ~420 mA on 3V3
static constexpr uint32_t kGaugeBlinkMs = 500;

enum CenterStyle : uint8_t {
  CENTER_BLINK,
  CENTER_RAINBOW
};

struct CenterEffect {
  CenterStyle style;
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t blinks;     // CENTER_BLINK: number of on/off cycles
  uint16_t onMs;
  uint16_t offMs;
  uint16_t totalMs;   // CENTER_RAINBOW: duration
};

static Adafruit_NeoPixel ring(kRingCount, kRingPin, NEO_GRB + NEO_KHZ800);

static uint8_t gaugePercent = 0;
static bool gaugeCharging = false;

static bool effectActive = false;
static CenterEffect effect = { CENTER_BLINK, 0, 0, 0, 0, 0, 0, 0 };
static uint32_t effectStartMs = 0;

static uint32_t lastShown[kRingCount];
static bool forceShow = true;

static bool effectFor(PpFeedbackEvent event, CenterEffect& out)
{
  switch (event) {
    case PP_FEEDBACK_POKEMON_NEARBY: {
      out = { CENTER_BLINK, 0, 255, 0, 3, 150, 150, 0 };
      return true;
    }
    case PP_FEEDBACK_POKESTOP_NEARBY: {
      out = { CENTER_BLINK, 0, 0, 255, 3, 150, 150, 0 };
      return true;
    }
    case PP_FEEDBACK_RESULT_UNKNOWN: {
      out = { CENTER_BLINK, 160, 0, 255, 2, 200, 200, 0 };
      return true;
    }
    case PP_FEEDBACK_CAUGHT: {
      out = { CENTER_RAINBOW, 0, 0, 0, 0, 0, 0, 1500 };
      return true;
    }
    case PP_FEEDBACK_FLED: {
      out = { CENTER_BLINK, 255, 0, 0, 3, 150, 150, 0 };
      return true;
    }
    case PP_FEEDBACK_POKESTOP_SPUN: {
      out = { CENTER_BLINK, 0, 255, 255, 2, 150, 150, 0 };
      return true;
    }
    case PP_FEEDBACK_CONNECTED: {
      out = { CENTER_BLINK, 255, 255, 255, 1, 300, 0, 0 };
      return true;
    }
    case PP_FEEDBACK_DISCONNECTED: {
      out = { CENTER_BLINK, 80, 0, 0, 1, 300, 0, 0 };
      return true;
    }
    case PP_FEEDBACK_FULLY_CHARGED: {
      out = { CENTER_BLINK, 0, 255, 0, 2, 300, 200, 0 };
      return true;
    }
    case PP_FEEDBACK_NONE: {
      return false;
    }
  }
  return false;
}

static uint32_t centerColor(uint32_t nowMs)
{
  if (!effectActive) {
    return 0;
  }
  uint32_t elapsed = nowMs - effectStartMs;
  if (effect.style == CENTER_RAINBOW) {
    if (elapsed >= effect.totalMs) {
      effectActive = false;
      return 0;
    }
    uint16_t hue = static_cast<uint16_t>((elapsed * 65536UL * 2) / effect.totalMs);
    return Adafruit_NeoPixel::ColorHSV(hue);
  }
  uint32_t cycle = static_cast<uint32_t>(effect.onMs) + effect.offMs;
  if (cycle == 0 || elapsed >= cycle * effect.blinks) {
    effectActive = false;
    return 0;
  }
  if (elapsed % cycle < effect.onMs) {
    return Adafruit_NeoPixel::Color(effect.r, effect.g, effect.b);
  }
  return 0;
}

static void render(uint32_t nowMs)
{
  uint32_t frame[kRingCount];
  frame[kCenterIndex] = centerColor(nowMs);

  bool lit[PP_GAUGE_SEGMENTS];
  bool blinkOn = (nowMs / kGaugeBlinkMs) % 2 == 0;
  ppBatteryGaugeFrame(gaugePercent, gaugeCharging, blinkOn, lit);
  // Gauge color: green above half, amber above a sixth, red below.
  uint32_t gaugeColor = Adafruit_NeoPixel::Color(0, 255, 0);
  if (gaugePercent < 50) {
    gaugeColor = Adafruit_NeoPixel::Color(255, 120, 0);
  }
  if (gaugePercent < 17) {
    gaugeColor = Adafruit_NeoPixel::Color(255, 0, 0);
  }
  for (uint8_t i = 0; i < PP_GAUGE_SEGMENTS; ++i) {
    frame[kFirstOuterIndex + i] = lit[i] ? gaugeColor : 0;
  }

  bool changed = forceShow;
  for (uint8_t i = 0; i < kRingCount; ++i) {
    if (frame[i] != lastShown[i]) {
      changed = true;
    }
  }
  if (!changed) {
    return;
  }
  for (uint8_t i = 0; i < kRingCount; ++i) {
    ring.setPixelColor(i, frame[i]);
    lastShown[i] = frame[i];
  }
  ring.show();
  forceShow = false;
}

}  // namespace

void ppFeedbackLedBegin()
{
  ring.begin();
  ring.setBrightness(kBrightness);
  ring.clear();
  ring.show();
  forceShow = true;
}

void ppFeedbackLedSetGauge(uint8_t percent, bool charging)
{
  gaugePercent = percent;
  gaugeCharging = charging;
}

void ppFeedbackLedPlayEvent(PpFeedbackEvent event, uint32_t nowMs)
{
  CenterEffect next;
  if (!effectFor(event, next)) {
    return;
  }
  effect = next;
  effectStartMs = nowMs;
  effectActive = true;
}

void ppFeedbackLedUpdate(uint32_t nowMs)
{
  render(nowMs);
}

void ppFeedbackLedOff()
{
  effectActive = false;
  ring.clear();
  ring.show();
  forceShow = true;
}
