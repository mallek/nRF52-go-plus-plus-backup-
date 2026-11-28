#include <Arduino.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

#include "pp_settings.h"

using namespace Adafruit_LittleFS_Namespace;

namespace {

static constexpr char kSettingsPath[] = "/settings.bin";

static PpSettings current = ppSettingsDefaults();

static void save()
{
  uint8_t record[PP_SETTINGS_MAX_RECORD];
  size_t n = ppSettingsPack(current, record, sizeof(record));
  if (n == 0) {
    Serial.println("Settings: could not save");
    return;
  }
  InternalFS.remove(kSettingsPath);
  File file(InternalFS);
  if (!file.open(kSettingsPath, FILE_O_WRITE)) {
    Serial.println("Settings: could not save");
    return;
  }
  file.write(record, n);
  file.close();
}

}  // namespace

void ppSettingsBegin()
{
  uint8_t record[PP_SETTINGS_MAX_RECORD];
  int n = 0;
  File file(InternalFS);
  if (file.open(kSettingsPath, FILE_O_READ)) {
    n = file.read(record, sizeof(record));
    file.close();
  }
  bool ok = n > 0 && ppSettingsUnpack(record, static_cast<size_t>(n), current);
  if (!ok) {
    current = ppSettingsDefaults();
  }
  Serial.println(ok ? "Settings: loaded" : "Settings: defaults (no valid record)");
}

const PpSettings& ppSettings()
{
  return current;
}

void ppSettingsSetAnimations(bool on)
{
  if (current.animations == on) {
    return;
  }
  current.animations = on;
  save();
}

void ppSettingsSetPokegachi(bool on)
{
  if (current.pokegachi == on) {
    return;
  }
  current.pokegachi = on;
  save();
}
