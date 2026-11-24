#include <Arduino.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <string.h>

#include "pp_identity.h"
#include "pp_types.h"

#if __has_include("pp_identities.generated.h")
#include "pp_identities.generated.h"
#else
#error "Missing pp_identities.generated.h. Build with tools/fw.ps1, or run tools/identity_table/gen_identities.py."
#endif

using namespace Adafruit_LittleFS_Namespace;

uint8_t REAL_PLUSPLUS_ADDR_LE[6];
uint8_t PLUSPLUS_FACTORY_CERT_BLOCK[256];
uint8_t PLUSPLUS_FACTORY_SELECTOR3[16];
uint8_t PLUSPLUS_FACTORY_SELECTOR4[16];
uint8_t PLUSPLUS_FACTORY_SELECTOR6[10];

namespace {

// Holds the selected identity's MAC (6 bytes, little-endian) plus the table index it
// was picked at. Keyed by MAC, so a rebuild that reorders the table still finds the
// right entry. The index only breaks ties when two entries share one MAC.
static constexpr char kSelectionPath[] = "/identity.bin";
// Per-identity generated sleep nights; stale once the identity changes.
static constexpr char kSleepPolicyPath[] = "/sleep_regen.bin";

static uint8_t active = 0;

static void load(uint8_t index)
{
  const PpIdentity& id = PP_IDENTITY_TABLE[index];
  memcpy(REAL_PLUSPLUS_ADDR_LE, id.addrLe, sizeof(REAL_PLUSPLUS_ADDR_LE));
  memcpy(PLUSPLUS_FACTORY_CERT_BLOCK, id.certBlock, sizeof(PLUSPLUS_FACTORY_CERT_BLOCK));
  memcpy(PLUSPLUS_FACTORY_SELECTOR3, id.selector3, sizeof(PLUSPLUS_FACTORY_SELECTOR3));
  memcpy(PLUSPLUS_FACTORY_SELECTOR4, id.selector4, sizeof(PLUSPLUS_FACTORY_SELECTOR4));
  memcpy(PLUSPLUS_FACTORY_SELECTOR6, id.selector6, sizeof(PLUSPLUS_FACTORY_SELECTOR6));
  active = index;
}

// `index` comes back as 0xff when the file is the older 6-byte form.
static bool readSelection(uint8_t macLe[6], uint8_t& index)
{
  File file(InternalFS);
  if (!file.open(kSelectionPath, FILE_O_READ)) {
    return false;
  }
  uint8_t raw[7];
  int n = file.read(raw, sizeof(raw));
  file.close();
  if (n < 6) {
    return false;
  }
  memcpy(macLe, raw, 6);
  index = (n >= 7) ? raw[6] : 0xff;
  return true;
}

static void writeSelection(const uint8_t macLe[6], uint8_t index)
{
  InternalFS.remove(kSelectionPath);
  File file(InternalFS);
  if (!file.open(kSelectionPath, FILE_O_WRITE)) {
    Serial.println("Identity: could not save selection");
    return;
  }
  file.write(macLe, 6);
  file.write(&index, 1);
  file.close();
}

static void clearIdentityBoundState(const char* reason)
{
  InternalFS.remove(PERSISTED_PAIR_STATE_PATH);
  InternalFS.remove(kSleepPolicyPath);
  Serial.print("Identity: pair state cleared via ");
  Serial.println(reason);
}

}  // namespace

void ppIdentityBegin()
{
  InternalFS.begin();

  uint8_t saved[6];
  uint8_t savedIndex = 0xff;
  bool haveSaved = readSelection(saved, savedIndex);
  int found = -1;
  if (haveSaved && savedIndex < PP_IDENTITY_TABLE_COUNT &&
      memcmp(PP_IDENTITY_TABLE[savedIndex].addrLe, saved, 6) == 0) {
    found = savedIndex;
  }
  if (haveSaved && found < 0) {
    for (uint8_t i = 0; i < PP_IDENTITY_TABLE_COUNT; ++i) {
      if (memcmp(PP_IDENTITY_TABLE[i].addrLe, saved, 6) == 0) {
        found = i;
        break;
      }
    }
  }

  if (found < 0) {
    load(0);
    clearIdentityBoundState(haveSaved ? "saved identity not in this build" : "no saved identity");
    writeSelection(REAL_PLUSPLUS_ADDR_LE, 0);
  } else {
    load(static_cast<uint8_t>(found));
  }

  Serial.print("Identity: ");
  Serial.print(PP_IDENTITY_TABLE[active].name);
  Serial.print(" (");
  Serial.print(active + 1);
  Serial.print("/");
  Serial.print(PP_IDENTITY_TABLE_COUNT);
  Serial.println(")");
}

uint8_t ppIdentityCount()
{
  return PP_IDENTITY_TABLE_COUNT;
}

uint8_t ppIdentityActive()
{
  return active;
}

const char* ppIdentityName(uint8_t index)
{
  if (index >= PP_IDENTITY_TABLE_COUNT) {
    return "?";
  }
  return PP_IDENTITY_TABLE[index].name;
}

void ppIdentitySwitchTo(uint8_t index)
{
  if (index >= PP_IDENTITY_TABLE_COUNT) {
    return;
  }
  Serial.print("Identity: switching to ");
  Serial.println(PP_IDENTITY_TABLE[index].name);
  writeSelection(PP_IDENTITY_TABLE[index].addrLe, index);
  clearIdentityBoundState("identity switch");
  Serial.flush();
  delay(50);
  NVIC_SystemReset();
}
