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
}

static void testRoundTrip()
{
  PpSettings s = { false, true };
  uint8_t buf[PP_SETTINGS_MAX_RECORD];
  size_t n = ppSettingsPack(s, buf, sizeof(buf));
  CHECK(n == 5);  // version, len=2, 2 fields, checksum
  CHECK(buf[0] == PP_SETTINGS_VERSION);
  CHECK(buf[1] == 2);
  PpSettings out = ppSettingsDefaults();
  CHECK(ppSettingsUnpack(buf, n, out));
  CHECK(out.animations == false && out.pokegachi == true);
}

static void testPackTooSmall()
{
  uint8_t buf[4];
  CHECK(ppSettingsPack(ppSettingsDefaults(), buf, sizeof(buf)) == 0);
}

static void testRejects()
{
  PpSettings s = { false, true };
  uint8_t buf[PP_SETTINGS_MAX_RECORD];
  size_t n = ppSettingsPack(s, buf, sizeof(buf));
  PpSettings out = { false, true };
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
  PpSettings out = { true, true };
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

int main()
{
  testDefaults();
  testRoundTrip();
  testPackTooSmall();
  testRejects();
  testOlderShortRecord();
  testOutOfRangeFieldKeepsDefault();
  if (failures == 0) {
    std::printf("settings_logic: all passed\n");
    return 0;
  }
  std::printf("settings_logic: %d failure(s)\n", failures);
  return 1;
}
