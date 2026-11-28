// Host tests for pp_settings_logic. Build + run from this folder (WSL or any g++):
//   g++ -std=c++17 -Wall -Wextra -Werror -I../../code/nrf_emulator/pgpemu_xiao
//       settings_logic_test.cpp ../../code/nrf_emulator/pgpemu_xiao/pp_settings_logic.cpp -o t
//   ./t

#include <cstdio>

#include "pp_settings_logic.h"

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

static void testDefaults()
{
  PpSettings d = ppSettingsDefaults();
  CHECK(d.animations == true);
  CHECK(d.pokegachi == false);
  CHECK(d.screenSleep == 1);
  CHECK(ppSettingsScreenSleepMs(d) == 30000);
}

static void testRoundTrip()
{
  PpSettings s = { false, true, 3 };
  uint8_t buf[PP_SETTINGS_MAX_RECORD];
  size_t n = ppSettingsPack(s, buf, sizeof(buf));
  CHECK(n == 6);  // version, len=3, 3 fields, checksum
  CHECK(buf[0] == PP_SETTINGS_VERSION);
  CHECK(buf[1] == 3);
  PpSettings out = ppSettingsDefaults();
  CHECK(ppSettingsUnpack(buf, n, out));
  CHECK(out.animations == false && out.pokegachi == true);
  CHECK(out.screenSleep == 3);
}

static void testPackTooSmall()
{
  uint8_t buf[5];
  CHECK(ppSettingsPack(ppSettingsDefaults(), buf, sizeof(buf)) == 0);
}

static void testRejects()
{
  PpSettings s = { false, true, 0 };
  uint8_t buf[PP_SETTINGS_MAX_RECORD];
  size_t n = ppSettingsPack(s, buf, sizeof(buf));
  PpSettings out = { false, true, 0 };
  CHECK(!ppSettingsUnpack(buf, 0, out));  // empty file
  CHECK(out.animations == true && out.pokegachi == false);
  CHECK(!ppSettingsUnpack(buf, n - 1, out));  // truncated
  buf[n - 1] ^= 0x01;
  CHECK(!ppSettingsUnpack(buf, n, out));  // bad checksum
  buf[n - 1] ^= 0x01;
  buf[0] = PP_SETTINGS_VERSION + 1;
  CHECK(!ppSettingsUnpack(buf, n, out));  // wrong version
  uint8_t lie[4] = { PP_SETTINGS_VERSION, 200, 0, 0 };
  CHECK(!ppSettingsUnpack(lie, sizeof(lie), out));  // length byte past the data
}

static void testOlderShortRecord()
{
  // A record written when only `animations` existed.
  uint8_t rec[4] = { PP_SETTINGS_VERSION, 1, 0, 0 };
  rec[3] = static_cast<uint8_t>((rec[0] + rec[1] + rec[2]) ^ 0xA5);
  PpSettings out = { true, true, 0 };
  CHECK(ppSettingsUnpack(rec, sizeof(rec), out));
  CHECK(out.animations == false);
  CHECK(out.pokegachi == false);  // default for the missing field
}

static void testOutOfRangeFieldKeepsDefault()
{
  uint8_t rec[5] = { PP_SETTINGS_VERSION, 2, 7, 1, 0 };
  rec[4] = static_cast<uint8_t>((rec[0] + rec[1] + rec[2] + rec[3]) ^ 0xA5);
  PpSettings out = ppSettingsDefaults();
  CHECK(ppSettingsUnpack(rec, sizeof(rec), out));
  CHECK(out.animations == true);  // 7 is not 0 or 1
  CHECK(out.pokegachi == true);
}

static void testScreenSleep()
{
  CHECK(PP_SCREEN_SLEEP_COUNT == 4);
  PpSettings s = ppSettingsDefaults();
  s.screenSleep = 0;
  CHECK(ppSettingsScreenSleepMs(s) == 10000);
  s.screenSleep = 2;
  CHECK(ppSettingsScreenSleepMs(s) == 60000);
  s.screenSleep = 3;
  CHECK(ppSettingsScreenSleepMs(s) == 120000);
  s.screenSleep = 200;  // a bad value in memory still gives a sane time
  CHECK(ppSettingsScreenSleepMs(s) == 30000);

  // A saved value past the last choice keeps the default.
  uint8_t rec[6] = { PP_SETTINGS_VERSION, 3, 1, 0, 9, 0 };
  rec[5] = static_cast<uint8_t>((rec[0] + rec[1] + rec[2] + rec[3] + rec[4]) ^ 0xA5);
  PpSettings out = ppSettingsDefaults();
  CHECK(ppSettingsUnpack(rec, sizeof(rec), out));
  CHECK(out.screenSleep == 1);

  // A record saved by the first menu build (two fields) loads, with the
  // default screen sleep.
  uint8_t old[5] = { PP_SETTINGS_VERSION, 2, 0, 1, 0 };
  old[4] = static_cast<uint8_t>((old[0] + old[1] + old[2] + old[3]) ^ 0xA5);
  PpSettings out2 = { true, false, 3 };
  CHECK(ppSettingsUnpack(old, sizeof(old), out2));
  CHECK(out2.animations == false && out2.pokegachi == true);
  CHECK(out2.screenSleep == 1);
}

int main()
{
  testDefaults();
  testRoundTrip();
  testPackTooSmall();
  testRejects();
  testOlderShortRecord();
  testOutOfRangeFieldKeepsDefault();
  testScreenSleep();
  if (failures == 0) {
    std::printf("settings_logic: all passed\n");
    return 0;
  }
  std::printf("settings_logic: %d failure(s)\n", failures);
  return 1;
}
