#include <string.h>

#include "pp_feedback_log_store.h"

// Image layout (little-endian):
//   "FBL1" | u8 patternCount | u8 droppedBlocks | u16 eventCount
//   patternCount x { u8 len | u16 count | len bytes }
//   eventCount   x { u32 ms | u8 kind | u8 len }

namespace {

static const uint8_t kMagic[4] = { 'F', 'B', 'L', '1' };

static void putU16(uint8_t* p, uint16_t v)
{
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}

static void putU32(uint8_t* p, uint32_t v)
{
  for (uint8_t i = 0; i < 4; ++i) {
    p[i] = static_cast<uint8_t>(v >> (8 * i));
  }
}

static uint16_t getU16(const uint8_t* p)
{
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

static uint32_t getU32(const uint8_t* p)
{
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static size_t patternBytes(const PpLogPattern& p)
{
  return 3 + p.len;
}

static void dropOldestBlock(PpLogStore& store)
{
  uint16_t n = store.eventCount < PP_LOG_DROP_BLOCK ? store.eventCount : PP_LOG_DROP_BLOCK;
  if (n == 0) {
    return;
  }
  memmove(store.events, store.events + n, static_cast<size_t>(store.eventCount - n) * sizeof(PpLogEvent));
  store.eventCount = static_cast<uint16_t>(store.eventCount - n);
  if (store.droppedBlocks < 255) {
    ++store.droppedBlocks;
  }
}

static bool addEvent(PpLogStore& store, uint8_t kind, uint8_t len, uint32_t ms, size_t extraPatternBytes)
{
  while (store.eventCount >= PP_LOG_MAX_EVENTS ||
         ppLogStoreSerializedSize(store) + extraPatternBytes + PP_LOG_EVENT_BYTES > PP_LOG_MAX_BYTES) {
    if (store.eventCount == 0) {
      return false;
    }
    dropOldestBlock(store);
  }
  store.events[store.eventCount] = { ms, kind, len };
  ++store.eventCount;
  return true;
}

}  // namespace

void ppLogStoreClear(PpLogStore& store)
{
  store.patternCount = 0;
  store.eventCount = 0;
  store.droppedBlocks = 0;
}

bool ppLogStoreAddMarker(PpLogStore& store, uint8_t kind, uint32_t ms)
{
  return addEvent(store, kind, 0, ms, 0);
}

bool ppLogStoreAddSample(PpLogStore& store, uint8_t kind, uint8_t value, uint32_t ms)
{
  return addEvent(store, kind, value, ms, 0);
}

void ppLogStoreCountPayload(PpLogStore& store, const uint8_t* data, uint8_t len)
{
  if (data == nullptr || len == 0) {
    return;
  }
  uint8_t storedLen = len > PP_LOG_MAX_PATTERN_LEN ? PP_LOG_MAX_PATTERN_LEN : len;
  for (uint8_t i = 0; i < store.patternCount; ++i) {
    PpLogPattern& p = store.patterns[i];
    if (p.len == storedLen && memcmp(p.bytes, data, storedLen) == 0) {
      if (p.count < 0xffff) {
        ++p.count;
      }
      return;
    }
  }
  if (store.patternCount >= PP_LOG_MAX_PATTERNS ||
      ppLogStoreSerializedSize(store) + 3 + storedLen > PP_LOG_MAX_BYTES) {
    return;
  }
  PpLogPattern& p = store.patterns[store.patternCount++];
  p.len = storedLen;
  memcpy(p.bytes, data, storedLen);
  p.count = 1;
}

bool ppLogStoreAddPayload(PpLogStore& store, const uint8_t* data, uint8_t len, uint32_t ms)
{
  if (data == nullptr || len == 0) {
    return false;
  }
  uint8_t storedLen = len > PP_LOG_MAX_PATTERN_LEN ? PP_LOG_MAX_PATTERN_LEN : len;
  for (uint8_t i = 0; i < store.patternCount; ++i) {
    PpLogPattern& p = store.patterns[i];
    if (p.len == storedLen && memcmp(p.bytes, data, storedLen) == 0) {
      if (!addEvent(store, i, len, ms, 0)) {
        return false;
      }
      if (p.count < 0xffff) {
        ++p.count;
      }
      return true;
    }
  }
  if (store.patternCount >= PP_LOG_MAX_PATTERNS) {
    return addEvent(store, PP_LOG_KIND_UNSTORED, len, ms, 0);
  }
  uint8_t id = store.patternCount;
  if (!addEvent(store, id, len, ms, 3 + storedLen)) {
    return false;
  }
  PpLogPattern& p = store.patterns[id];
  p.len = storedLen;
  memcpy(p.bytes, data, storedLen);
  p.count = 1;
  ++store.patternCount;
  return true;
}

size_t ppLogStoreSerializedSize(const PpLogStore& store)
{
  size_t size = PP_LOG_HEADER_BYTES;
  for (uint8_t i = 0; i < store.patternCount; ++i) {
    size += patternBytes(store.patterns[i]);
  }
  return size + static_cast<size_t>(store.eventCount) * PP_LOG_EVENT_BYTES;
}

size_t ppLogStoreSerialize(const PpLogStore& store, uint8_t* out, size_t cap)
{
  size_t size = ppLogStoreSerializedSize(store);
  if (out == nullptr || size > cap) {
    return 0;
  }
  uint8_t* p = out;
  memcpy(p, kMagic, 4);
  p[4] = store.patternCount;
  p[5] = store.droppedBlocks;
  putU16(p + 6, store.eventCount);
  p += PP_LOG_HEADER_BYTES;
  for (uint8_t i = 0; i < store.patternCount; ++i) {
    const PpLogPattern& pat = store.patterns[i];
    p[0] = pat.len;
    putU16(p + 1, pat.count);
    memcpy(p + 3, pat.bytes, pat.len);
    p += patternBytes(pat);
  }
  for (uint16_t i = 0; i < store.eventCount; ++i) {
    const PpLogEvent& e = store.events[i];
    putU32(p, e.ms);
    p[4] = e.kind;
    p[5] = e.len;
    p += PP_LOG_EVENT_BYTES;
  }
  return size;
}

bool ppLogStoreDeserialize(PpLogStore& store, const uint8_t* in, size_t len)
{
  ppLogStoreClear(store);
  if (in == nullptr || len < PP_LOG_HEADER_BYTES || memcmp(in, kMagic, 4) != 0) {
    return false;
  }
  uint8_t patternCount = in[4];
  uint16_t eventCount = getU16(in + 6);
  if (patternCount > PP_LOG_MAX_PATTERNS || eventCount > PP_LOG_MAX_EVENTS) {
    return false;
  }
  size_t pos = PP_LOG_HEADER_BYTES;
  for (uint8_t i = 0; i < patternCount; ++i) {
    if (pos + 3 > len) {
      ppLogStoreClear(store);
      return false;
    }
    uint8_t plen = in[pos];
    if (plen > PP_LOG_MAX_PATTERN_LEN || pos + 3 + plen > len) {
      ppLogStoreClear(store);
      return false;
    }
    PpLogPattern& p = store.patterns[i];
    p.len = plen;
    p.count = getU16(in + pos + 1);
    memcpy(p.bytes, in + pos + 3, plen);
    pos += 3 + plen;
  }
  if (pos + static_cast<size_t>(eventCount) * PP_LOG_EVENT_BYTES > len) {
    ppLogStoreClear(store);
    return false;
  }
  for (uint16_t i = 0; i < eventCount; ++i) {
    const uint8_t* e = in + pos;
    store.events[i] = { getU32(e), e[4], e[5] };
    pos += PP_LOG_EVENT_BYTES;
  }
  store.patternCount = patternCount;
  store.eventCount = eventCount;
  store.droppedBlocks = in[5];
  return true;
}
