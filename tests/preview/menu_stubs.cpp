// Stand-ins for the flash-backed units, so pp_menu_items.cpp builds on the PC.
// Only the functions the menu items call are defined here.

#include "menu_stubs.h"

#include "pp_gachi.h"
#include "pp_identity.h"
#include "pp_settings.h"

PpGachiState gStubGachi;
bool gStubGachiEnabled = true;

static PpSettings gSettings = {true, false, 1};

uint8_t ppIdentityCount() { return 3; }
uint8_t ppIdentityActive() { return 0; }
const char* ppIdentityName(uint8_t index) {
  static const char* const kNames[] = {"TEST1", "TEST2", "TEST3"};
  return index < 3 ? kNames[index] : "";
}

const PpSettings& ppSettings() { return gSettings; }
void ppSettingsSetAnimations(bool on) { gSettings.animations = on; }
void ppSettingsSetPokegachi(bool on) { gSettings.pokegachi = on; }
void ppSettingsSetScreenSleep(uint8_t index) { gSettings.screenSleep = index; }

bool ppGachiEnabled() { return gStubGachiEnabled; }
void ppGachiSetEnabled(bool on) { gStubGachiEnabled = on; }
const PpGachiState& ppGachiState() { return gStubGachi; }
