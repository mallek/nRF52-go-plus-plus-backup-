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

static int litCount(const bool lit[PP_GAUGE_SEGMENTS])
{
  int n = 0;
  for (int i = 0; i < PP_GAUGE_SEGMENTS; ++i) {
    if (lit[i]) {
      ++n;
    }
  }
  return n;
}

static void testGaugeNotCharging()
{
  bool lit[PP_GAUGE_SEGMENTS];
  ppBatteryGaugeFrame(0, false, true, lit);
  CHECK(litCount(lit) == 0);
  ppBatteryGaugeFrame(16, false, true, lit);
  CHECK(litCount(lit) == 0);
  ppBatteryGaugeFrame(17, false, true, lit);
  CHECK(litCount(lit) == 1 && lit[0]);
  ppBatteryGaugeFrame(50, false, true, lit);
  CHECK(litCount(lit) == 3 && lit[0] && lit[1] && lit[2]);
  ppBatteryGaugeFrame(100, false, false, lit);
  CHECK(litCount(lit) == 6);
}

static void testGaugeCharging()
{
  bool lit[PP_GAUGE_SEGMENTS];
  // Empty and charging: the first segment blinks.
  ppBatteryGaugeFrame(0, true, true, lit);
  CHECK(litCount(lit) == 1 && lit[0]);
  ppBatteryGaugeFrame(0, true, false, lit);
  CHECK(litCount(lit) == 0);
  // 50%: three solid, the fourth blinks.
  ppBatteryGaugeFrame(50, true, true, lit);
  CHECK(litCount(lit) == 4 && lit[3]);
  ppBatteryGaugeFrame(50, true, false, lit);
  CHECK(litCount(lit) == 3 && !lit[3]);
  // Full and still charging: all solid, nothing blinks off.
  ppBatteryGaugeFrame(100, true, false, lit);
  CHECK(litCount(lit) == 6);
  // Out-of-range clamps.
  ppBatteryGaugeFrame(250, false, false, lit);
  CHECK(litCount(lit) == 6);
}

static void testClassify()
{
  uint8_t buf[128];
  std::memset(buf, 0, sizeof(buf));

  buf[3] = 0x0f;
  CHECK(ppClassifyLedWrite(buf, 49) == PP_FEEDBACK_POKEMON_NEARBY);
  buf[3] = 0x1b;
  CHECK(ppClassifyLedWrite(buf, 85) == PP_FEEDBACK_POKESTOP_NEARBY);
  buf[3] = 0x18;
  CHECK(ppClassifyLedWrite(buf, 76) == PP_FEEDBACK_RESULT_UNKNOWN);

  // The action ack is not an event.
  const uint8_t ack[] = { 0x00, 0x00, 0x00, 0x01, 0x0a, 0x00, 0x00 };
  CHECK(ppClassifyLedWrite(ack, sizeof(ack)) == PP_FEEDBACK_NONE);

  // Length must match the declared count.
  buf[3] = 0x0f;
  CHECK(ppClassifyLedWrite(buf, 48) == PP_FEEDBACK_NONE);
  // Non-zero prefix is not an effect write.
  buf[0] = 0x01;
  CHECK(ppClassifyLedWrite(buf, 49) == PP_FEEDBACK_NONE);
  buf[0] = 0x00;
  CHECK(ppClassifyLedWrite(buf, 3) == PP_FEEDBACK_NONE);
  CHECK(ppClassifyLedWrite(nullptr, 49) == PP_FEEDBACK_NONE);
}

int main()
{
  testPercentCurve();
  testGaugeNotCharging();
  testGaugeCharging();
  testClassify();
  if (failures == 0) {
    std::printf("feedback_logic_test: all passed\n");
    return 0;
  }
  std::printf("feedback_logic_test: %d failure(s)\n", failures);
  return 1;
}
