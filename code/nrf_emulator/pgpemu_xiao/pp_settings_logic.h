#pragma once

#include <stddef.h>
#include <stdint.h>

// User settings and their saved record. Pure: no Arduino, no hardware. Compiled
// into the sketch and also by the host tests in tests/settings/.
//
// To add a setting: add a field at the END of PpSettings, give it a default in
// ppSettingsDefaults(), and add one line to each of the two field tables in
// pp_settings_logic.cpp. Older saved records keep loading; the new field takes
// its default.
struct PpSettings {
  bool animations;  // catch, spin, ready and boot animations
  bool pokegachi;   // the pet game (stored only for now)
  uint8_t screenSleep;  // index into the screen sleep times below
};

// Screen sleep choices, in menu order: 10 s, 30 s, 1 min, 2 min.
static constexpr uint8_t PP_SCREEN_SLEEP_COUNT = 4;
static constexpr uint8_t PP_SCREEN_SLEEP_DEFAULT = 1;

static constexpr uint8_t PP_SETTINGS_VERSION = 1;
static constexpr size_t PP_SETTINGS_MAX_RECORD = 32;

PpSettings ppSettingsDefaults();

// How long the screen stays lit after a press. A bad index gives the default.
uint32_t ppSettingsScreenSleepMs(const PpSettings& s);

// Record: [0] version, [1] payload length N, [2 .. 2+N) one byte per field in
// struct order (on/off fields 0 or 1, choice fields their index), [2+N] checksum. Returns bytes written, 0 if cap is too small.
size_t ppSettingsPack(const PpSettings& s, uint8_t* out, size_t cap);

// Always leaves `out` valid. Returns false (and out = defaults) on a bad record.
// A shorter payload from an older build keeps defaults for the fields it lacks.
bool ppSettingsUnpack(const uint8_t* in, size_t len, PpSettings& out);
