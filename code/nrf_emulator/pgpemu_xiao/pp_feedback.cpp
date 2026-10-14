#include <Arduino.h>

#include "pp_feedback.h"
#include "pp_feedback_battery.h"
#include "pp_feedback_led.h"
#include "pp_feedback_log.h"
#include "pp_feedback_log_store.h"

namespace {

// Single-producer (BLE task) / single-consumer (loop task) event queue.
static constexpr uint8_t kQueueSize = 8;
static volatile PpFeedbackEvent queue[kQueueSize];
static volatile uint8_t queueHead = 0;
static volatile uint8_t queueTail = 0;

static bool started = false;
static bool wasCharging = false;

static bool popEvent(PpFeedbackEvent& out)
{
  if (queueTail == queueHead) {
    return false;
  }
  out = queue[queueTail];
  queueTail = static_cast<uint8_t>((queueTail + 1) % kQueueSize);
  return true;
}

static void logBattery(const PpBatteryState& b)
{
  Serial.print("Feedback battery mv=");
  Serial.print(b.millivolts);
  Serial.print(" pct=");
  Serial.print(b.percent);
  Serial.print(" charging=");
  Serial.print(b.charging ? "yes" : "no");
  Serial.print(" usb=");
  Serial.println(b.usbPresent ? "yes" : "no");
}

}  // namespace

void ppFeedbackBegin()
{
  ppFeedbackBatteryBegin();
  ppFeedbackLedBegin();
  ppFeedbackLogBegin();
  started = true;
  Serial.println("Feedback: ring D7, status light on");
}

void ppFeedbackPost(PpFeedbackEvent event)
{
  if (event == PP_FEEDBACK_NONE) {
    return;
  }
  uint8_t next = static_cast<uint8_t>((queueHead + 1) % kQueueSize);
  if (next == queueTail) {
    return;  // full: drop rather than block a BLE callback
  }
  queue[queueHead] = event;
  queueHead = next;
}

void ppFeedbackOnLedWrite(const uint8_t* data, uint16_t len)
{
  PpFeedbackEvent event = ppClassifyLedWrite(data, len);
  Serial.print("Feedback LED write len=");
  Serial.print(len);
  Serial.print(" class=");
  Serial.print(ppFeedbackEventName(event));
  Serial.print(" bytes=");
  for (uint16_t i = 0; i < len; ++i) {
    if (data[i] < 0x10) {
      Serial.print('0');
    }
    Serial.print(data[i], HEX);
    Serial.print(i + 1 < len ? " " : "");
  }
  Serial.println();
  ppFeedbackLogPayload(data, len);
  ppFeedbackPost(event);
}

void ppFeedbackUpdate()
{
  if (!started) {
    return;
  }
  uint32_t now = millis();

  if (ppFeedbackBatteryUpdate(now)) {
    const PpBatteryState& b = ppFeedbackBatteryState();
    ppFeedbackLedSetBattery(b.percent, b.charging, b.usbPresent);
    if (wasCharging && !b.charging && b.usbPresent) {
      ppFeedbackPost(PP_FEEDBACK_FULLY_CHARGED);
    }
    wasCharging = b.charging;
    static uint32_t lastLogMs = 0;
    if (now - lastLogMs >= 30000 || lastLogMs == 0) {
      lastLogMs = now;
      logBattery(b);
    }
  }

  PpFeedbackEvent event;
  while (popEvent(event)) {
    Serial.print("Feedback event: ");
    Serial.println(ppFeedbackEventName(event));
    if (event == PP_FEEDBACK_CONNECTED) {
      ppFeedbackLogMarker(PP_LOG_KIND_CONNECT);
    } else if (event == PP_FEEDBACK_DISCONNECTED) {
      ppFeedbackLogMarker(PP_LOG_KIND_DISCONNECT);
    }
    ppFeedbackLedPlayEvent(event, now);
  }

  ppFeedbackLedUpdate(now);
  ppFeedbackLogUpdate(now);
}

void ppFeedbackShutdown()
{
  if (!started) {
    return;
  }
  ppFeedbackLedOff();
  ppFeedbackLogFlush();
}
