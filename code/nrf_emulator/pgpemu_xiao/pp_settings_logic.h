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
};

static constexpr uint8_t PP_SETTINGS_VERSION = 1;
static constexpr size_t PP_SETTINGS_MAX_RECORD = 32;

PpSettings ppSettingsDefaults();

// Record: [0] version, [1] payload length N, [2 .. 2+N) one byte per field in
// struct order, [2+N] checksum. Returns bytes written, 0 if cap is too small.
size_t ppSettingsPack(const PpSettings& s, uint8_t* out, size_t cap);

// Always leaves `out` valid. Returns false (and out = defaults) on a bad record.
// A shorter payload from an older build keeps defaults for the fields it lacks.
bool ppSettingsUnpack(const uint8_t* in, size_t len, PpSettings& out);
