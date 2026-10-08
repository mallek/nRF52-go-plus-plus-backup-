#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure, host-testable field log of LED/effect writes. Distinct payloads are stored once
// in a pattern table; each occurrence is a 6-byte event. Serialized size is capped so
// the log can never crowd the pairing state out of the internal file system.

static constexpr uint8_t PP_LOG_MAX_PATTERNS = 12;
static constexpr uint8_t PP_LOG_MAX_PATTERN_LEN = 128;
static constexpr size_t PP_LOG_MAX_BYTES = 8192;

// Event kinds above the pattern id range.
static constexpr uint8_t PP_LOG_KIND_BOOT = 0xF0;
static constexpr uint8_t PP_LOG_KIND_CONNECT = 0xF1;
static constexpr uint8_t PP_LOG_KIND_DISCONNECT = 0xF2;
static constexpr uint8_t PP_LOG_KIND_UNSTORED = 0xFE;  // pattern table full

struct PpLogPattern {
  uint8_t len;
  uint8_t bytes[PP_LOG_MAX_PATTERN_LEN];
  uint16_t count;
};

struct PpLogEvent {
  uint32_t ms;   // millis() since that boot
  uint8_t kind;  // pattern id, or PP_LOG_KIND_*
  uint8_t len;   // payload length (patterns), 0 otherwise
};

static constexpr size_t PP_LOG_HEADER_BYTES = 8;
static constexpr size_t PP_LOG_EVENT_BYTES = 6;
static constexpr size_t PP_LOG_MAX_EVENTS = 1200;

struct PpLogStore {
  PpLogPattern patterns[PP_LOG_MAX_PATTERNS];
  uint8_t patternCount;
  PpLogEvent events[PP_LOG_MAX_EVENTS];
  uint16_t eventCount;
  bool full;
};

void ppLogStoreClear(PpLogStore& store);

// Returns false (and sets full) when the event would exceed the size cap.
bool ppLogStoreAddMarker(PpLogStore& store, uint8_t kind, uint32_t ms);
bool ppLogStoreAddPayload(PpLogStore& store, const uint8_t* data, uint8_t len, uint32_t ms);

size_t ppLogStoreSerializedSize(const PpLogStore& store);
// Writes the store into out (cap bytes). Returns bytes written, 0 if it does not fit.
size_t ppLogStoreSerialize(const PpLogStore& store, uint8_t* out, size_t cap);
// Rebuilds the store from bytes. Returns false on a bad/unknown image (store left cleared).
bool ppLogStoreDeserialize(PpLogStore& store, const uint8_t* in, size_t len);
