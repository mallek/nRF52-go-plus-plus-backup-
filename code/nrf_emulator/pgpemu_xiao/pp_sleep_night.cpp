#include "pp_sleep_night.h"

namespace {

struct Rng {
  uint32_t state;

  uint32_t next()
  {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }

  // Inclusive range.
  uint16_t range(uint16_t lo, uint16_t hi)
  {
    return static_cast<uint16_t>(lo + next() % static_cast<uint32_t>(hi - lo + 1));
  }
};

static const uint8_t kKindMarker = 4;
static const uint8_t kKindStart = 1;
static const uint8_t kKindSettle = 0;

static uint16_t entry(uint8_t kind, uint16_t minute)
{
  return static_cast<uint16_t>((static_cast<uint16_t>(kind) << 12) | (minute & 0x0fff));
}

static void push(PpSleepNight* night, uint8_t kind, uint16_t minute)
{
  if (night->count < PP_SLEEP_NIGHT_MAX_ENTRIES) {
    night->entries[night->count++] = entry(kind, minute);
  }
}

}  // namespace

void ppSleepNightGenerate(uint32_t seed, PpSleepNight* out)
{
  if (out == nullptr) {
    return;
  }
  Rng rng = { seed == 0 ? 0x9e3779b9u : seed };
  // Warm up: xorshift outputs for nearby seeds are correlated at first.
  for (int i = 0; i < 8; ++i) {
    rng.next();
  }

  out->count = 0;
  for (uint8_t i = 0; i < PP_SLEEP_NIGHT_MAX_ENTRIES; ++i) {
    out->entries[i] = 0;
  }

  // Length: sum of two uniforms so it clusters mid-range rather than at the edges.
  uint16_t span = PP_SLEEP_NIGHT_MAX_LENGTH - PP_SLEEP_NIGHT_MIN_LENGTH;
  uint16_t half = span / 2;
  out->lengthMin = static_cast<uint16_t>(PP_SLEEP_NIGHT_MIN_LENGTH + rng.range(0, half) + rng.range(0, span - half));
  out->slackMin = rng.range(PP_SLEEP_NIGHT_MIN_SLACK, PP_SLEEP_NIGHT_MAX_SLACK);

  const uint16_t length = out->lengthMin;

  push(out, kKindMarker, 0);

  // Onset blip shortly after the start (real nights: 1 at 5 then 0 at 7).
  uint16_t onset = rng.range(4, 35);
  push(out, kKindStart, onset);
  push(out, kKindSettle, static_cast<uint16_t>(onset + rng.range(1, 3)));

  // 0-2 mid-night blip pairs, one per equal segment of the quiet window so they stay ordered.
  uint32_t roll = rng.next() % 10;
  uint8_t pairs = roll < 4 ? 0 : (roll < 8 ? 1 : 2);
  uint16_t windowStart = static_cast<uint16_t>(onset + 30);
  uint16_t windowEnd = static_cast<uint16_t>(length - 45);
  if (pairs > 0) {
    uint16_t segment = static_cast<uint16_t>((windowEnd - windowStart) / pairs);
    for (uint8_t p = 0; p < pairs; ++p) {
      uint16_t segStart = static_cast<uint16_t>(windowStart + p * segment);
      uint16_t t = rng.range(segStart, static_cast<uint16_t>(segStart + segment - 20));
      uint16_t d = rng.range(1, 15);
      push(out, kKindStart, t);
      push(out, rng.range(0, 4) == 0 ? kKindMarker : kKindSettle, static_cast<uint16_t>(t + d));
    }
  }

  // Closing: movement, then the end marker twice (second one at the end or one minute before).
  push(out, kKindStart, static_cast<uint16_t>(length - 2));
  push(out, kKindMarker, static_cast<uint16_t>(length - rng.range(0, 1)));
  push(out, kKindMarker, length);
}

uint32_t ppSleepNightStart(const PpSleepNight& night, uint32_t anchor)
{
  return anchor - (static_cast<uint32_t>(night.lengthMin) + night.slackMin) * 60U;
}
