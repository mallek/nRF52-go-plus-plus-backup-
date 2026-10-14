#include <Arduino.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <string.h>

#include "pp_feedback_log.h"
#include "pp_feedback_log_store.h"

using namespace Adafruit_LittleFS_Namespace;

namespace {

static const char* kLogPath = "/fblog.bin";
static constexpr uint32_t kFlushIntervalMs = 60000;
static constexpr uint16_t kFlushEveryEvents = 50;

static PpLogStore store;
static uint8_t image[PP_LOG_MAX_BYTES];
static bool ready = false;
static bool dirty = false;
static uint16_t eventsSinceFlush = 0;
static uint32_t lastFlushMs = 0;
static uint8_t bootIndex = 0;  // boots seen in the stored log, this one included

// BLE task -> loop task payload queue.
static constexpr uint8_t kQueueSize = 6;
struct QueuedPayload {
  uint32_t ms;
  uint8_t len;
  uint8_t bytes[PP_LOG_MAX_PATTERN_LEN];
};
static QueuedPayload queue[kQueueSize];
static volatile uint8_t queueHead = 0;
static volatile uint8_t queueTail = 0;

static char lineBuf[16];
static uint8_t lineLen = 0;

static void writeFile()
{
  size_t n = ppLogStoreSerialize(store, image, sizeof(image));
  if (n == 0) {
    Serial.println("Feedback log: serialize failed");
    return;
  }
  InternalFS.remove(kLogPath);
  File file(InternalFS);
  if (!file.open(kLogPath, FILE_O_WRITE)) {
    Serial.println("Feedback log: open for write failed");
    return;
  }
  file.write(image, n);
  file.close();
  dirty = false;
  eventsSinceFlush = 0;
}

static void loadFile()
{
  ppLogStoreClear(store);
  File file(InternalFS);
  if (!file.open(kLogPath, FILE_O_READ)) {
    return;
  }
  uint32_t size = file.size();
  if (size <= sizeof(image)) {
    uint32_t got = file.read(image, size);
    if (got != size || !ppLogStoreDeserialize(store, image, size)) {
      Serial.println("Feedback log: stored image unreadable, starting fresh");
      ppLogStoreClear(store);
    }
  }
  file.close();
}

// The dark single-entry action ack (00 00 00 01 xx 00 00) follows almost every effect
// write and carries no information; counting it keeps the log from filling with noise.
static bool isDarkAck(const uint8_t* bytes, uint8_t len)
{
  return len == 7 && bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 1 &&
         bytes[5] == 0 && bytes[6] == 0;
}

static void noteEvent(bool added)
{
  if (added) {
    dirty = true;
    ++eventsSinceFlush;
  }
}

static void printHexBytes(const uint8_t* bytes, uint8_t len)
{
  for (uint8_t i = 0; i < len; ++i) {
    if (bytes[i] < 0x10) {
      Serial.print('0');
    }
    Serial.print(bytes[i], HEX);
    if (i + 1 < len) {
      Serial.print(' ');
    }
  }
}

static void printDuration(uint32_t ms)
{
  uint32_t s = ms / 1000;
  Serial.print(s / 3600);
  Serial.print('h');
  if ((s / 60) % 60 < 10) {
    Serial.print('0');
  }
  Serial.print((s / 60) % 60);
  Serial.print('m');
  if (s % 60 < 10) {
    Serial.print('0');
  }
  Serial.print(s % 60);
  Serial.print('s');
}

static void dump(uint32_t nowMs)
{
  Serial.print("=== FIELD LOG patterns=");
  Serial.print(store.patternCount);
  Serial.print(" events=");
  Serial.print(store.eventCount);
  Serial.print(" bytes=");
  Serial.print(ppLogStoreSerializedSize(store));
  if (store.droppedBlocks > 0) {
    Serial.print(" WRAPPED (oldest ~");
    Serial.print(static_cast<uint32_t>(store.droppedBlocks) * PP_LOG_DROP_BLOCK);
    Serial.print(" events dropped)");
  }
  Serial.println(" ===");
  for (uint8_t i = 0; i < store.patternCount; ++i) {
    const PpLogPattern& p = store.patterns[i];
    Serial.print("P");
    Serial.print(i);
    Serial.print(" count=");
    Serial.print(p.count);
    Serial.print(" len=");
    Serial.print(p.len);
    Serial.print(" bytes=");
    printHexBytes(p.bytes, p.len);
    Serial.println();
  }
  uint8_t boot = 0;
  for (uint16_t i = 0; i < store.eventCount; ++i) {
    const PpLogEvent& e = store.events[i];
    if (e.kind == PP_LOG_KIND_BOOT) {
      ++boot;
    }
    Serial.print("boot");
    Serial.print(boot);
    Serial.print(" +");
    printDuration(e.ms);
    if (boot == bootIndex && e.ms <= nowMs) {
      Serial.print(" (");
      printDuration(nowMs - e.ms);
      Serial.print(" ago)");
    }
    Serial.print(' ');
    if (e.kind == PP_LOG_KIND_BOOT) {
      Serial.println("BOOT");
    } else if (e.kind == PP_LOG_KIND_CONNECT) {
      Serial.println("CONNECT");
    } else if (e.kind == PP_LOG_KIND_DISCONNECT) {
      Serial.println("DISCONNECT");
    } else if (e.kind == PP_LOG_KIND_BATTERY || e.kind == PP_LOG_KIND_BATTERY_USB) {
      uint32_t mv = 3000 + static_cast<uint32_t>(e.len) * 10;
      Serial.print("BATT ");
      Serial.print(mv / 1000);
      Serial.print('.');
      if (mv % 1000 < 100) {
        Serial.print('0');
      }
      if (mv % 1000 < 10) {
        Serial.print('0');
      }
      Serial.print(mv % 1000);
      Serial.println(e.kind == PP_LOG_KIND_BATTERY_USB ? "V usb" : "V");
    } else if (e.kind == PP_LOG_KIND_UNSTORED) {
      Serial.print("UNSTORED len=");
      Serial.println(e.len);
    } else {
      Serial.print("P");
      Serial.println(e.kind);
    }
  }
  Serial.println("=== END FIELD LOG ===");
}

static void handleCommand(const char* cmd, uint32_t nowMs)
{
  if (strcmp(cmd, "dump") == 0) {
    dump(nowMs);
  } else if (strcmp(cmd, "clear") == 0) {
    ppLogStoreClear(store);
    InternalFS.remove(kLogPath);
    bootIndex = 1;
    noteEvent(ppLogStoreAddMarker(store, PP_LOG_KIND_BOOT, 0));
    writeFile();
    Serial.println("Feedback log: cleared");
  } else if (strcmp(cmd, "flush") == 0) {
    writeFile();
    Serial.println("Feedback log: flushed");
  } else if (cmd[0] != '\0') {
    Serial.print("Feedback log: unknown command '");
    Serial.print(cmd);
    Serial.println("' (dump | clear | flush)");
  }
}

static void pollSerial(uint32_t nowMs)
{
  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c == '\r' || c == '\n') {
      lineBuf[lineLen] = '\0';
      handleCommand(lineBuf, nowMs);
      lineLen = 0;
    } else if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = static_cast<char>(c);
    }
  }
}

}  // namespace

void ppFeedbackLogBegin()
{
  loadFile();
  bootIndex = 0;
  for (uint16_t i = 0; i < store.eventCount; ++i) {
    if (store.events[i].kind == PP_LOG_KIND_BOOT) {
      ++bootIndex;
    }
  }
  ++bootIndex;
  noteEvent(ppLogStoreAddMarker(store, PP_LOG_KIND_BOOT, millis()));
  ready = true;
  Serial.print("Feedback log: ");
  Serial.print(store.eventCount);
  Serial.print(" events, ");
  Serial.print(ppLogStoreSerializedSize(store));
  Serial.println(" bytes (type dump / clear / flush)");
}

void ppFeedbackLogPayload(const uint8_t* data, uint16_t len)
{
  if (data == nullptr || len == 0) {
    return;
  }
  uint8_t next = static_cast<uint8_t>((queueHead + 1) % kQueueSize);
  if (next == queueTail) {
    return;  // full: drop rather than block a BLE callback
  }
  QueuedPayload& q = queue[queueHead];
  q.ms = millis();
  q.len = static_cast<uint8_t>(len > PP_LOG_MAX_PATTERN_LEN ? PP_LOG_MAX_PATTERN_LEN : len);
  memcpy(q.bytes, data, q.len);
  queueHead = next;
}

void ppFeedbackLogMarker(uint8_t kind)
{
  if (!ready) {
    return;
  }
  noteEvent(ppLogStoreAddMarker(store, kind, millis()));
}

void ppFeedbackLogBattery(uint16_t millivolts, bool usbPresent)
{
  if (!ready) {
    return;
  }
  uint16_t steps = millivolts < 3000 ? 0 : static_cast<uint16_t>((millivolts - 3000) / 10);
  uint8_t value = steps > 255 ? 255 : static_cast<uint8_t>(steps);
  noteEvent(ppLogStoreAddSample(store, usbPresent ? PP_LOG_KIND_BATTERY_USB : PP_LOG_KIND_BATTERY,
                                value, millis()));
}

void ppFeedbackLogUpdate(uint32_t nowMs)
{
  if (!ready) {
    return;
  }
  while (queueTail != queueHead) {
    const QueuedPayload& q = queue[queueTail];
    if (isDarkAck(q.bytes, q.len)) {
      ppLogStoreCountPayload(store, q.bytes, q.len);
    } else {
      noteEvent(ppLogStoreAddPayload(store, q.bytes, q.len, q.ms));
    }
    queueTail = static_cast<uint8_t>((queueTail + 1) % kQueueSize);
  }
  if (dirty && (eventsSinceFlush >= kFlushEveryEvents || nowMs - lastFlushMs >= kFlushIntervalMs)) {
    writeFile();
    lastFlushMs = nowMs;
  }
  pollSerial(nowMs);
}

void ppFeedbackLogFlush()
{
  if (ready && dirty) {
    writeFile();
  }
}
