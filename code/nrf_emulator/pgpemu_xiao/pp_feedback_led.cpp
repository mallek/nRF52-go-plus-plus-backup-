#include <Adafruit_NeoPixel.h>

#include "pp_feedback_led.h"

namespace {

static constexpr uint8_t kRingPin = D7;
static constexpr uint8_t kRingCount = 7;
static constexpr uint8_t kCenterIndex = 0;      // most 7-LED jewels put pixel 0 in the middle
static constexpr uint8_t kFirstOuterIndex = 1;  // outer pixels 1..6
static constexpr uint8_t kBrightness = 40;      // 7 px at full white would be ~420 mA on 3V3
static constexpr uint16_t kChaseMs = 2000;
static constexpr uint8_t kChaseRotations = 2;
static constexpr uint32_t kPulsePeriodMs = 2000;
static constexpr uint32_t kPulseStepMs = 40;    // quantized so show() is not called every loop

enum EffectStyle : uint8_t {
  EFFECT_WHOLE_BLINK,   // all 7 pixels blink one color
  EFFECT_OUTER_CHASE    // rainbow chasing around the 6 outer pixels, center keeps the status light
};

struct Effect {
  EffectStyle style;
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t blinks;     // WHOLE_BLINK: number of on/off cycles
  uint16_t onMs;
  uint16_t offMs;
  uint16_t totalMs;   // OUTER_CHASE: duration
};

static Adafruit_NeoPixel ring(kRingCount, kRingPin, NEO_GRB + NEO_KHZ800);

static PpStatusLight status = PP_STATUS_OFF;

static bool effectActive = false;
static Effect effect = { EFFECT_WHOLE_BLINK, 0, 0, 0, 0, 0, 0, 0 };
static uint32_t effectStartMs = 0;

static uint32_t lastShown[kRingCount];
static bool forceShow = true;

static bool effectFor(PpFeedbackEvent event, Effect& out)
{
  switch (event) {
    case PP_FEEDBACK_CAUGHT: {
      out = { EFFECT_OUTER_CHASE, 0, 0, 0, 0, 0, 0, kChaseMs };
      return true;
    }
    case PP_FEEDBACK_FLED: {
      out = { EFFECT_WHOLE_BLINK, 255, 0, 0, 3, 200, 150, 0 };
      return true;
    }
    case PP_FEEDBACK_CONNECTED: {
      out = { EFFECT_WHOLE_BLINK, 255, 255, 255, 1, 300, 0, 0 };
      return true;
    }
    case PP_FEEDBACK_DISCONNECTED: {
      out = { EFFECT_WHOLE_BLINK, 120, 0, 0, 1, 600, 0, 0 };
      return true;
    }
    case PP_FEEDBACK_FULLY_CHARGED: {
      out = { EFFECT_WHOLE_BLINK, 0, 255, 0, 2, 300, 200, 0 };
      return true;
    }
    case PP_FEEDBACK_BAG_FULL: {
      out = { EFFECT_WHOLE_BLINK, 255, 120, 0, 1, 1000, 0, 0 };  // one long amber
      return true;
    }
    case PP_FEEDBACK_UNKNOWN_SINGLE: {
      out = { EFFECT_WHOLE_BLINK, 255, 0, 128, 2, 500, 300, 0 };  // magenta: undecoded single flash
      return true;
    }
    case PP_FEEDBACK_RESULT_UNKNOWN: {
      out = { EFFECT_WHOLE_BLINK, 160, 0, 255, 2, 200, 200, 0 };  // purple: undecoded multi-entry write
      return true;
    }
    // Decoded but deliberately dark. Still logged by the field log.
    case PP_FEEDBACK_POKEMON_NEARBY:
    case PP_FEEDBACK_POKESTOP_NEARBY:
    case PP_FEEDBACK_POKESTOP_SPUN:
    case PP_FEEDBACK_NONE: {
      return false;
    }
  }
  return false;
}

// Static status light on the center pixel. Charging breathes green.
static uint32_t statusColor(uint32_t nowMs)
{
  switch (status) {
    case PP_STATUS_AMBER: {
      return Adafruit_NeoPixel::Color(120, 45, 0);
    }
    case PP_STATUS_RED: {
      return Adafruit_NeoPixel::Color(120, 0, 0);
    }
    case PP_STATUS_CHARGING: {
      uint32_t t = ((nowMs / kPulseStepMs) * kPulseStepMs) % kPulsePeriodMs;
      uint32_t half = kPulsePeriodMs / 2;
      uint32_t ramp = t < half ? t : kPulsePeriodMs - t;
      uint8_t g = static_cast<uint8_t>(20 + (ramp * 235) / half);
      return Adafruit_NeoPixel::Color(0, g, 0);
    }
    case PP_STATUS_OFF: {
      return 0;
    }
  }
  return 0;
}

// Fills the frame for the active effect. Returns false once it has finished.
static bool applyEffect(uint32_t nowMs, uint32_t frame[kRingCount])
{
  uint32_t elapsed = nowMs - effectStartMs;
  if (effect.style == EFFECT_OUTER_CHASE) {
    if (elapsed >= effect.totalMs) {
      return false;
    }
    uint32_t phase = (elapsed * 65536UL * kChaseRotations) / effect.totalMs;
    for (uint8_t i = 0; i < kRingCount - kFirstOuterIndex; ++i) {
      uint16_t hue = static_cast<uint16_t>(phase - (i * 65536UL) / (kRingCount - kFirstOuterIndex));
      frame[kFirstOuterIndex + i] = Adafruit_NeoPixel::ColorHSV(hue);
    }
    return true;
  }
  uint32_t cycle = static_cast<uint32_t>(effect.onMs) + effect.offMs;
  if (cycle == 0 || elapsed >= cycle * effect.blinks) {
    return false;
  }
  if (elapsed % cycle < effect.onMs) {
    uint32_t color = Adafruit_NeoPixel::Color(effect.r, effect.g, effect.b);
    for (uint8_t i = 0; i < kRingCount; ++i) {
      frame[i] = color;
    }
  }
  return true;
}

static void render(uint32_t nowMs)
{
  uint32_t frame[kRingCount] = {};
  frame[kCenterIndex] = statusColor(nowMs);
  if (effectActive && !applyEffect(nowMs, frame)) {
    effectActive = false;
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

void ppFeedbackLedSetBattery(uint8_t percent, bool charging, bool usbPresent)
{
  status = ppStatusLight(percent, charging, usbPresent);
}

void ppFeedbackLedPlayEvent(PpFeedbackEvent event, uint32_t nowMs)
{
  Effect next;
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
