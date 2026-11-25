// Host tests for pp_feedback_logic. Build + run from this folder (WSL or any g++):
//   g++ -std=c++17 -Wall -Wextra -Werror -I../../code/nrf_emulator/pgpemu_xiao
//       feedback_logic_test.cpp ../../code/nrf_emulator/pgpemu_xiao/pp_feedback_logic.cpp -o t
//   ./t

#include <cstdio>
#include <cstring>

#include "pp_feedback_logic.h"

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

static void testPercentCurve()
{
  CHECK(ppBatteryPercentFromMillivolts(4300) == 100);
  CHECK(ppBatteryPercentFromMillivolts(4200) == 100);
  CHECK(ppBatteryPercentFromMillivolts(3840) == 50);
  CHECK(ppBatteryPercentFromMillivolts(3270) == 0);
  CHECK(ppBatteryPercentFromMillivolts(3000) == 0);
  // 3.89 V (the fresh cells measured on the bench) sits between 65% and 60%.
  uint8_t p = ppBatteryPercentFromMillivolts(3890);
  CHECK(p > 60 && p < 65);
  // Monotonic: never goes up as voltage drops.
  uint8_t prev = 100;
  for (int mv = 4250; mv >= 3200; mv -= 5) {
    uint8_t now = ppBatteryPercentFromMillivolts(static_cast<uint16_t>(mv));
    CHECK(now <= prev);
    prev = now;
  }
}

static void testStatusLight()
{
  // Above 30% on battery: dark.
  CHECK(ppStatusLight(100, false, false) == PP_STATUS_OFF);
  CHECK(ppStatusLight(30, false, false) == PP_STATUS_OFF);
  // Below 30%: amber. Below 10%: red.
  CHECK(ppStatusLight(29, false, false) == PP_STATUS_AMBER);
  CHECK(ppStatusLight(10, false, false) == PP_STATUS_AMBER);
  CHECK(ppStatusLight(9, false, false) == PP_STATUS_RED);
  CHECK(ppStatusLight(0, false, false) == PP_STATUS_RED);
  // On USB and charging: charging pulse wins over the low-battery color.
  CHECK(ppStatusLight(5, true, true) == PP_STATUS_CHARGING);
  CHECK(ppStatusLight(80, true, true) == PP_STATUS_CHARGING);
  // On USB, charge finished: dark again (the fully-charged event already flashed).
  CHECK(ppStatusLight(100, false, true) == PP_STATUS_OFF);
  // On USB but not charging and still low: still warn.
  CHECK(ppStatusLight(20, false, true) == PP_STATUS_AMBER);
  // The CHG line without USB is never a charging state.
  CHECK(ppStatusLight(50, true, false) == PP_STATUS_OFF);
}

// Real payloads from the 2026-10-08 field log (fieldlogs/fieldlog-20261008-*.txt),
// labeled against the in-game Journal and live observation.
static const uint8_t kEncounterP0[] = {
  0x00, 0x00, 0x00, 0x1B, 0x02, 0xCC, 0xF0, 0x02, 0x00, 0x00, 0x03, 0xCC, 0xF0, 0x02, 0x00, 0x00,
  0x03, 0xCC, 0xF0, 0x02, 0x00, 0x00, 0x0C, 0xCC, 0xF0, 0x09, 0x00, 0x00, 0x02, 0xCC, 0xF0, 0x02,
  0x00, 0x00, 0x03, 0xCC, 0xF0, 0x02, 0x00, 0x00, 0x03, 0xCC, 0xF0, 0x02, 0x00, 0x00, 0x0C, 0xCC,
  0xF0, 0x09, 0x00, 0x00, 0x02, 0xCC, 0xF0, 0x02, 0x00, 0x00, 0x03, 0xCC, 0xF0, 0x02, 0x00, 0x00,
  0x03, 0xCC, 0xF0, 0x02, 0x00, 0x00, 0x0C, 0xCC, 0xF0, 0x02, 0x00, 0x00, 0x40, 0x88, 0x80, 0xFF,
  0x88, 0x80, 0xB7, 0x88, 0x80
};
static const uint8_t kPokestopP2[] = {
  0x00, 0x00, 0x00, 0x1B, 0x04, 0x00, 0xFF, 0x02, 0x00, 0x80, 0x08, 0x00, 0xFF, 0x0A, 0x00, 0x80,
  0x04, 0x00, 0xFF, 0x02, 0x00, 0x80, 0x08, 0x00, 0xFF, 0x0A, 0x00, 0x80, 0x04, 0x00, 0xFF, 0x02,
  0x00, 0x80, 0x08, 0x00, 0xFF, 0x0A, 0x00, 0x80, 0x04, 0x00, 0xFF, 0x02, 0x00, 0x80, 0x08, 0x00,
  0xFF, 0x0A, 0x00, 0x80, 0x04, 0x00, 0xFF, 0x02, 0x00, 0x80, 0x08, 0x00, 0xFF, 0x0A, 0x00, 0x80,
  0x04, 0x00, 0xFF, 0x02, 0x00, 0x80, 0x08, 0x00, 0xFF, 0x0A, 0x00, 0x80, 0x40, 0x00, 0x88, 0xFF,
  0x00, 0x88, 0x89, 0x00, 0x88
};
static const uint8_t kCaughtP5[] = {
  0x00, 0x00, 0x00, 0x18, 0x03, 0x88, 0xF8, 0x09, 0x00, 0x80, 0x10, 0x00, 0x00, 0x03, 0x88, 0xF8,
  0x09, 0x00, 0x80, 0x10, 0x00, 0x00, 0x03, 0x88, 0xF8, 0x09, 0x00, 0x80, 0x10, 0x00, 0x00, 0x03,
  0x08, 0xF0, 0x03, 0xF0, 0xF0, 0x03, 0x00, 0x8F, 0x03, 0x08, 0xF0, 0x03, 0xF0, 0xF0, 0x03, 0x00,
  0x8F, 0x03, 0x08, 0xF0, 0x03, 0xF0, 0xF0, 0x03, 0x00, 0x8F, 0x03, 0x08, 0xF0, 0x03, 0xF0, 0xF0,
  0x03, 0x00, 0x8F, 0x03, 0x08, 0xF0, 0x03, 0xF0, 0xF0, 0x03, 0x00, 0x8F
};
static const uint8_t kFledP6[] = {
  0x00, 0x00, 0x00, 0x09, 0x03, 0x88, 0xF8, 0x09, 0x00, 0x80, 0x10, 0x00, 0x00, 0x01, 0x0F, 0xF0,
  0x02, 0x00, 0x80, 0x01, 0x0F, 0xF0, 0x02, 0x00, 0x80, 0x01, 0x0F, 0xF0, 0x02, 0x00, 0x80
};
static const uint8_t kFledP10[] = {
  0x00, 0x00, 0x00, 0x0F, 0x03, 0x88, 0xF8, 0x09, 0x00, 0x80, 0x10, 0x00, 0x00, 0x03, 0x88, 0xF8,
  0x09, 0x00, 0x80, 0x10, 0x00, 0x00, 0x03, 0x88, 0xF8, 0x09, 0x00, 0x80, 0x10, 0x00, 0x00, 0x01,
  0x0F, 0xF0, 0x02, 0x00, 0x80, 0x01, 0x0F, 0xF0, 0x02, 0x00, 0x80, 0x01, 0x0F, 0xF0, 0x02, 0x00,
  0x80
};
static const uint8_t kSpunP9[] = {
  0x00, 0x00, 0x00, 0x09, 0x03, 0xF0, 0x80, 0x03, 0x0F, 0xF0, 0x03, 0x00, 0x8F, 0x03, 0xF0, 0x80,
  0x03, 0x0F, 0xF0, 0x03, 0x00, 0x8F, 0x03, 0xF0, 0x80, 0x03, 0x0F, 0xF0, 0x03, 0x00, 0x8F
};
static const uint8_t kSpunP7[] = {
  0x00, 0x00, 0x00, 0x0C, 0x03, 0xF0, 0x80, 0x03, 0x0F, 0xF0, 0x03, 0x00, 0x8F, 0x03, 0xF0, 0x80,
  0x03, 0x0F, 0xF0, 0x03, 0x00, 0x8F, 0x03, 0xF0, 0x80, 0x03, 0x0F, 0xF0, 0x03, 0x00, 0x8F, 0x03,
  0xF0, 0x80, 0x03, 0x0F, 0xF0, 0x03, 0x00, 0x8F
};
static const uint8_t kEncounterP8[] = {
  0x00, 0x00, 0x00, 0x0F, 0x10, 0xF0, 0xF0, 0x08, 0x00, 0x00, 0x10, 0xF0, 0xF0, 0x08, 0x00, 0x00,
  0x10, 0xF0, 0xF0, 0x08, 0x00, 0x00, 0x10, 0xF0, 0xF0, 0x08, 0x00, 0x00, 0x10, 0xF0, 0xF0, 0x08,
  0x00, 0x00, 0x10, 0xF0, 0xF0, 0x08, 0x00, 0x00, 0x40, 0x80, 0x80, 0xFF, 0x80, 0x80, 0x89, 0x80,
  0x80
};

static void testClassify()
{
  CHECK(ppClassifyLedWrite(kEncounterP0, sizeof(kEncounterP0)) == PP_FEEDBACK_POKEMON_NEARBY);
  CHECK(ppClassifyLedWrite(kPokestopP2, sizeof(kPokestopP2)) == PP_FEEDBACK_POKESTOP_NEARBY);
  CHECK(ppClassifyLedWrite(kCaughtP5, sizeof(kCaughtP5)) == PP_FEEDBACK_CAUGHT);
  CHECK(ppClassifyLedWrite(kFledP6, sizeof(kFledP6)) == PP_FEEDBACK_FLED);
  CHECK(ppClassifyLedWrite(kFledP10, sizeof(kFledP10)) == PP_FEEDBACK_FLED);
  CHECK(ppClassifyLedWrite(kSpunP9, sizeof(kSpunP9)) == PP_FEEDBACK_POKESTOP_SPUN);
  CHECK(ppClassifyLedWrite(kSpunP7, sizeof(kSpunP7)) == PP_FEEDBACK_POKESTOP_SPUN);
  CHECK(ppClassifyLedWrite(kEncounterP8, sizeof(kEncounterP8)) == PP_FEEDBACK_POKEMON_NEARBY);

  // The dark single-entry action ack is not an event.
  const uint8_t ack[] = { 0x00, 0x00, 0x00, 0x01, 0x0a, 0x00, 0x00 };
  CHECK(ppClassifyLedWrite(ack, sizeof(ack)) == PP_FEEDBACK_NONE);
  // The long amber single flash: box or bag full (after a Pokemon alert or a stop).
  const uint8_t storageFull[] = { 0x00, 0x00, 0x00, 0x01, 0x18, 0x0f, 0xf0 };
  CHECK(ppClassifyLedWrite(storageFull, sizeof(storageFull)) == PP_FEEDBACK_STORAGE_FULL);
  // 10 FF FF follows Pokestop in range (135 of 244 on a drive): the stop left range.
  const uint8_t stopPassed[] = { 0x00, 0x00, 0x00, 0x01, 0x10, 0xff, 0xff };
  CHECK(ppClassifyLedWrite(stopPassed, sizeof(stopPassed)) == PP_FEEDBACK_STOP_PASSED);
  // Any other colored single entry is still undecoded.
  const uint8_t unknownSingle[] = { 0x00, 0x00, 0x00, 0x01, 0x22, 0x12, 0x34 };
  CHECK(ppClassifyLedWrite(unknownSingle, sizeof(unknownSingle)) == PP_FEEDBACK_UNKNOWN_SINGLE);
  // Red blinks with dark gaps: out of balls (Jack's rule; synthetic bytes, not from a field log).
  const uint8_t ballsEmpty[] = { 0x00, 0x00, 0x00, 0x04,
                                 0x04, 0x0f, 0x70, 0x04, 0x00, 0x00,
                                 0x04, 0x0f, 0x70, 0x04, 0x00, 0x00 };
  CHECK(ppClassifyLedWrite(ballsEmpty, sizeof(ballsEmpty)) == PP_FEEDBACK_BALLS_EMPTY);
  CHECK(std::strcmp(ppFeedbackEventName(PP_FEEDBACK_BALLS_EMPTY), "balls-empty") == 0);
  // Red with no gap, or another color mixed in, is still undecoded.
  const uint8_t redNoGap[] = { 0x00, 0x00, 0x00, 0x02, 0x04, 0x0f, 0x70, 0x04, 0x0f, 0x70 };
  CHECK(ppClassifyLedWrite(redNoGap, sizeof(redNoGap)) == PP_FEEDBACK_RESULT_UNKNOWN);
  const uint8_t redAndGreen[] = { 0x00, 0x00, 0x00, 0x03,
                                  0x04, 0x0f, 0x70, 0x04, 0x00, 0x00, 0x04, 0xf0, 0x70 };
  CHECK(ppClassifyLedWrite(redAndGreen, sizeof(redAndGreen)) == PP_FEEDBACK_RESULT_UNKNOWN);
  const uint8_t empty[] = { 0x00, 0x00, 0x00, 0x00 };
  CHECK(ppClassifyLedWrite(empty, sizeof(empty)) == PP_FEEDBACK_NONE);

  // Length must match the declared count.
  CHECK(ppClassifyLedWrite(kCaughtP5, sizeof(kCaughtP5) - 1) == PP_FEEDBACK_NONE);
  // Non-zero prefix is not an effect write.
  uint8_t bad[sizeof(kCaughtP5)];
  std::memcpy(bad, kCaughtP5, sizeof(bad));
  bad[0] = 0x01;
  CHECK(ppClassifyLedWrite(bad, sizeof(bad)) == PP_FEEDBACK_NONE);
  CHECK(ppClassifyLedWrite(kCaughtP5, 3) == PP_FEEDBACK_NONE);
  CHECK(ppClassifyLedWrite(nullptr, 49) == PP_FEEDBACK_NONE);
}

static void testFullState()
{
  PpFullState s;
  ppFullStateInit(s);
  CHECK(!ppFullStateNote(s, PP_FEEDBACK_CAUGHT, 1000));

  // Full after a Pokemon alert: Pokemon box. A catch clears it, a spin does not.
  ppFullStateNote(s, PP_FEEDBACK_POKEMON_NEARBY, 1000);
  CHECK(ppFullStateNote(s, PP_FEEDBACK_STORAGE_FULL, 2000));
  CHECK(s.pokemonFull && !s.bagFull);
  CHECK(s.lastFull == PP_FEEDBACK_POKEMON_FULL);
  CHECK(ppFullStateNote(s, PP_FEEDBACK_POKESTOP_SPUN, 3000));
  CHECK(!ppFullStateNote(s, PP_FEEDBACK_CAUGHT, 4000));

  // The same flash after a stop is a failed spin: nothing is full.
  ppFullStateNote(s, PP_FEEDBACK_POKESTOP_NEARBY, 10000);
  CHECK(!ppFullStateNote(s, PP_FEEDBACK_STORAGE_FULL, 11000));
  CHECK(!s.pokemonFull && !s.bagFull);
  CHECK(s.lastFull == PP_FEEDBACK_SPIN_FAILED);
  CHECK(std::strcmp(ppFeedbackEventName(PP_FEEDBACK_SPIN_FAILED), "spin-failed") == 0);

  // A fled throw also clears the Pokemon flag.
  ppFullStateNote(s, PP_FEEDBACK_POKEMON_NEARBY, 20000);
  ppFullStateNote(s, PP_FEEDBACK_STORAGE_FULL, 21000);
  CHECK(!ppFullStateNote(s, PP_FEEDBACK_FLED, 22000));

  // Stale or missing context: the Pokemon box. A catch clears it.
  ppFullStateNote(s, PP_FEEDBACK_POKESTOP_NEARBY, 30000);
  CHECK(ppFullStateNote(s, PP_FEEDBACK_STORAGE_FULL, 30000 + PP_FULL_CONTEXT_MS + 1));
  CHECK(s.pokemonFull && !s.bagFull);
  CHECK(s.lastFull == PP_FEEDBACK_POKEMON_FULL);
  CHECK(!ppFullStateNote(s, PP_FEEDBACK_CAUGHT, 60000));
  ppFullStateInit(s);
  CHECK(ppFullStateNote(s, PP_FEEDBACK_STORAGE_FULL, 5));
  CHECK(s.pokemonFull && !s.bagFull);

  // Dark events and stop-passed never change the flags.
  ppFullStateInit(s);
  CHECK(!ppFullStateNote(s, PP_FEEDBACK_STOP_PASSED, 100));
}

int main()
{
  testPercentCurve();
  testStatusLight();
  testClassify();
  testFullState();
  if (failures == 0) {
    std::printf("feedback_logic_test: all passed\n");
    return 0;
  }
  std::printf("feedback_logic_test: %d failure(s)\n", failures);
  return 1;
}
