#pragma once

#include "pp_settings_logic.h"

// Saved user settings (InternalFS, /settings.bin). Per device, not per identity:
// an identity switch and a factory reset both keep them.
//
// Call ppSettingsBegin() after ppIdentityBegin(), which mounts InternalFS.
// Before that, ppSettings() returns the defaults.
void ppSettingsBegin();
const PpSettings& ppSettings();

// Each setter saves only when the value changes. Call from loop(), never from a
// BLE callback: a flash write blocks.
void ppSettingsSetAnimations(bool on);
void ppSettingsSetPokegachi(bool on);
