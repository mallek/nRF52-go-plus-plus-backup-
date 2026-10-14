// Host tests for pp_sleep_night. Build + run from this folder (WSL or any g++):
//   g++ -std=c++17 -Wall -Wextra -Werror -I../../code/nrf_emulator/pgpemu_xiao
//       sleep_night_test.cpp ../../code/nrf_emulator/pgpemu_xiao/pp_sleep_night.cpp -o t
//   ./t

#include <cstdio>
#include <cstring>
#include <set>

#include "pp_sleep_night.h"

static int failures = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      ++failures;                                                       \
    }                                                                   \
  } while (0)

static uint8_t kindOf(uint16_t e) { return static_cast<uint8_t>(e >> 12); }
static uint16_t minuteOf(uint16_t e) { return static_cast<uint16_t>(e & 0x0fff); }

static void testInvariants()
{
  for (uint32_t seed = 0; seed < 20000; ++seed) {
    PpSleepNight n;
    ppSleepNightGenerate(seed, &n);
    CHECK(n.count >= 6 && n.count <= PP_SLEEP_NIGHT_MAX_ENTRIES);
    CHECK(n.lengthMin >= PP_SLEEP_NIGHT_MIN_LENGTH && n.lengthMin <= PP_SLEEP_NIGHT_MAX_LENGTH);
    CHECK(n.slackMin >= PP_SLEEP_NIGHT_MIN_SLACK && n.slackMin <= PP_SLEEP_NIGHT_MAX_SLACK);
    // Starts with (4,0), ends with (4,L).
    CHECK(n.entries[0] == 0x4000);
    CHECK(kindOf(n.entries[n.count - 1]) == 4 && minuteOf(n.entries[n.count - 1]) == n.lengthMin);
    // Minutes never go backwards and never pass the end.
    for (uint8_t i = 1; i < n.count; ++i) {
      CHECK(minuteOf(n.entries[i]) >= minuteOf(n.entries[i - 1]));
      CHECK(minuteOf(n.entries[i]) <= n.lengthMin);
    }
    // Every blip start (kind 1) is followed by a settle (kind 0 or 4).
    for (uint8_t i = 1; i < n.count; ++i) {
      if (kindOf(n.entries[i]) == 1) {
        CHECK(i + 1 < n.count);
        CHECK(kindOf(n.entries[i + 1]) == 0 || kindOf(n.entries[i + 1]) == 4);
      }
    }
    // Entries past count are zero (the stream buffer is zero-filled anyway).
    for (uint8_t i = n.count; i < PP_SLEEP_NIGHT_MAX_ENTRIES; ++i) {
      CHECK(n.entries[i] == 0);
    }
  }
}

static void testDeterministic()
{
  PpSleepNight a;
  PpSleepNight b;
  ppSleepNightGenerate(12345, &a);
  ppSleepNightGenerate(12345, &b);
  CHECK(a.lengthMin == b.lengthMin && a.slackMin == b.slackMin && a.count == b.count);
  CHECK(std::memcmp(a.entries, b.entries, sizeof(a.entries)) == 0);
  ppSleepNightGenerate(0, &a);  // seed 0 must not lock the generator
  CHECK(a.lengthMin >= PP_SLEEP_NIGHT_MIN_LENGTH);
}

static void testVariety()
{
  std::set<uint16_t> lengths;
  std::set<uint16_t> slacks;
  std::set<uint8_t> counts;
  double sum = 0;
  const int kN = 5000;
  for (uint32_t seed = 1000; seed < 1000 + kN; ++seed) {
    PpSleepNight n;
    ppSleepNightGenerate(seed * 2654435761u, &n);
    lengths.insert(n.lengthMin);
    slacks.insert(n.slackMin);
    counts.insert(n.count);
    sum += n.lengthMin;
  }
  CHECK(lengths.size() > 120);
  CHECK(slacks.size() > 30);
  CHECK(counts.size() == 3);  // 6, 8 or 10 entries
  double mean = sum / kN;
  CHECK(mean > 435 && mean < 455);
}

static void testConsecutiveNightsDiffer()
{
  PpSleepNight prev;
  ppSleepNightGenerate(1, &prev);
  int identical = 0;
  for (uint32_t seed = 2; seed < 500; ++seed) {
    PpSleepNight n;
    ppSleepNightGenerate(seed, &n);
    if (n.count == prev.count && std::memcmp(n.entries, prev.entries, sizeof(n.entries)) == 0) {
      ++identical;
    }
    prev = n;
  }
  CHECK(identical == 0);
}

static void testStart()
{
  PpSleepNight n;
  ppSleepNightGenerate(7, &n);
  uint32_t anchor = 1791000000u;
  CHECK(ppSleepNightStart(n, anchor) == anchor - (n.lengthMin + n.slackMin) * 60u);
}

int main()
{
  testInvariants();
  testDeterministic();
  testVariety();
  testConsecutiveNightsDiffer();
  testStart();
  if (failures == 0) {
    std::printf("sleep_night_test: all passed\n");
    return 0;
  }
  std::printf("sleep_night_test: %d failures\n", failures);
  return 1;
}
