// Host tests for pp_feedback_log_store. Build + run from this folder:
//   g++ -std=c++17 -Wall -Wextra -Werror -I../../code/nrf_emulator/pgpemu_xiao
//       log_store_test.cpp ../../code/nrf_emulator/pgpemu_xiao/pp_feedback_log_store.cpp -o t
//   ./t

#include <cstdio>
#include <cstring>

#include "pp_feedback_log_store.h"

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

static PpLogStore a;
static PpLogStore b;
static uint8_t image[PP_LOG_MAX_BYTES];

static void testDedupeAndCounts()
{
  ppLogStoreClear(a);
  const uint8_t x[] = { 0, 0, 0, 1, 0x0a, 0, 0 };
  const uint8_t y[] = { 0, 0, 0, 2, 1, 2, 3, 4, 5, 6 };
  CHECK(ppLogStoreAddMarker(a, PP_LOG_KIND_BOOT, 0));
  CHECK(ppLogStoreAddPayload(a, x, sizeof(x), 100));
  CHECK(ppLogStoreAddPayload(a, y, sizeof(y), 200));
  CHECK(ppLogStoreAddPayload(a, x, sizeof(x), 300));
  CHECK(a.patternCount == 2);
  CHECK(a.eventCount == 4);
  CHECK(a.patterns[0].count == 2);
  CHECK(a.patterns[1].count == 1);
  CHECK(a.events[3].kind == 0 && a.events[3].ms == 300 && a.events[3].len == sizeof(x));
  CHECK(a.events[0].kind == PP_LOG_KIND_BOOT);
}

static void testRoundTrip()
{
  size_t n = ppLogStoreSerialize(a, image, sizeof(image));
  CHECK(n == ppLogStoreSerializedSize(a));
  CHECK(n > 0);
  CHECK(ppLogStoreDeserialize(b, image, n));
  CHECK(b.patternCount == a.patternCount);
  CHECK(b.eventCount == a.eventCount);
  CHECK(b.patterns[1].len == a.patterns[1].len);
  CHECK(std::memcmp(b.patterns[1].bytes, a.patterns[1].bytes, a.patterns[1].len) == 0);
  CHECK(b.patterns[0].count == 2);
  CHECK(b.events[3].ms == 300 && b.events[3].kind == 0);
  // Appending after a reload keeps deduping against restored patterns.
  const uint8_t x[] = { 0, 0, 0, 1, 0x0a, 0, 0 };
  CHECK(ppLogStoreAddPayload(b, x, sizeof(x), 400));
  CHECK(b.patternCount == 2 && b.patterns[0].count == 3);
}

static void testRejectsBadImages()
{
  uint8_t junk[16] = { 'N', 'O', 'P', 'E' };
  CHECK(!ppLogStoreDeserialize(b, junk, sizeof(junk)));
  CHECK(b.eventCount == 0);
  size_t n = ppLogStoreSerialize(a, image, sizeof(image));
  CHECK(!ppLogStoreDeserialize(b, image, n - 1));  // truncated
  CHECK(b.eventCount == 0 && b.patternCount == 0);
  CHECK(ppLogStoreSerialize(a, image, 4) == 0);    // does not fit
}

static void testSizeCapNeverExceeded()
{
  ppLogStoreClear(a);
  uint8_t p[85] = { 0, 0, 0, 0x1b };
  uint32_t added = 0;
  for (uint32_t i = 0; i < 5000; ++i) {
    p[10] = static_cast<uint8_t>(i % 40);  // 40 distinct payloads > pattern table
    if (ppLogStoreAddPayload(a, p, sizeof(p), i)) {
      ++added;
    }
    CHECK(ppLogStoreSerializedSize(a) <= PP_LOG_MAX_BYTES);
  }
  // Full never refuses an event: the oldest block is dropped instead.
  CHECK(added == 5000);
  CHECK(a.droppedBlocks > 0);
  CHECK(a.patternCount == PP_LOG_MAX_PATTERNS);
  CHECK(a.events[a.eventCount - 1].ms == 4999);  // newest is kept
  CHECK(a.events[0].ms > 0);                     // oldest is gone
  CHECK(ppLogStoreAddMarker(a, PP_LOG_KIND_CONNECT, 5000));
  CHECK(a.events[a.eventCount - 1].kind == PP_LOG_KIND_CONNECT);
  bool sawUnstored = false;
  for (uint16_t i = 0; i < a.eventCount; ++i) {
    if (a.events[i].kind == PP_LOG_KIND_UNSTORED) {
      sawUnstored = true;
    }
  }
  CHECK(sawUnstored);
}

static void testWrapKeepsOrderAndRoundTrips()
{
  ppLogStoreClear(a);
  for (uint32_t i = 0; i < 3000; ++i) {
    ppLogStoreAddMarker(a, PP_LOG_KIND_CONNECT, i);
  }
  CHECK(a.eventCount <= PP_LOG_MAX_EVENTS);
  for (uint16_t i = 1; i < a.eventCount; ++i) {
    CHECK(a.events[i].ms == a.events[i - 1].ms + 1);  // contiguous, oldest block removed
  }
  uint8_t dropped = a.droppedBlocks;
  CHECK(dropped > 0);
  size_t n = ppLogStoreSerialize(a, image, sizeof(image));
  CHECK(n > 0);
  CHECK(ppLogStoreDeserialize(b, image, n));
  CHECK(b.droppedBlocks == dropped);
  CHECK(b.eventCount == a.eventCount);
}

static void testCountPayloadStoresNoEvent()
{
  ppLogStoreClear(a);
  const uint8_t ack[] = { 0, 0, 0, 1, 0x0a, 0, 0 };
  ppLogStoreCountPayload(a, ack, sizeof(ack));
  ppLogStoreCountPayload(a, ack, sizeof(ack));
  ppLogStoreCountPayload(a, ack, sizeof(ack));
  CHECK(a.eventCount == 0);
  CHECK(a.patternCount == 1 && a.patterns[0].count == 3);
  // A later stored occurrence dedupes against the same pattern.
  CHECK(ppLogStoreAddPayload(a, ack, sizeof(ack), 10));
  CHECK(a.patternCount == 1 && a.patterns[0].count == 4 && a.eventCount == 1);
  // Counting never overflows the size cap, even with a full pattern table.
  ppLogStoreClear(a);
  uint8_t p[40] = { 0, 0, 0, 5 };
  for (uint16_t i = 0; i < 200; ++i) {
    p[10] = static_cast<uint8_t>(i);
    ppLogStoreCountPayload(a, p, sizeof(p));
    CHECK(ppLogStoreSerializedSize(a) <= PP_LOG_MAX_BYTES);
  }
  CHECK(a.patternCount == PP_LOG_MAX_PATTERNS);
}

static void testSamples()
{
  ppLogStoreClear(a);
  CHECK(ppLogStoreAddSample(a, PP_LOG_KIND_BATTERY, 91, 1000));
  CHECK(ppLogStoreAddSample(a, PP_LOG_KIND_BATTERY_USB, 120, 2000));
  CHECK(a.eventCount == 2);
  CHECK(a.events[0].kind == PP_LOG_KIND_BATTERY && a.events[0].len == 91);
  size_t n = ppLogStoreSerialize(a, image, sizeof(image));
  CHECK(ppLogStoreDeserialize(b, image, n));
  CHECK(b.events[1].kind == PP_LOG_KIND_BATTERY_USB && b.events[1].len == 120 && b.events[1].ms == 2000);
}

int main()
{
  testDedupeAndCounts();
  testRoundTrip();
  testRejectsBadImages();
  testSizeCapNeverExceeded();
  testWrapKeepsOrderAndRoundTrips();
  testCountPayloadStoresNoEvent();
  testSamples();
  if (failures == 0) {
    std::printf("log_store_test: all passed\n");
    return 0;
  }
  std::printf("log_store_test: %d failure(s)\n", failures);
  return 1;
}
