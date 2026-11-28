#include "pp_settings_logic.h"

namespace {

static constexpr uint8_t kFieldCount = 2;
static constexpr uint8_t kChecksumXor = 0xA5;

uint8_t checksum(const uint8_t* bytes, size_t n)
{
  uint8_t sum = 0;
  for (size_t i = 0; i < n; ++i) {
    sum = static_cast<uint8_t>(sum + bytes[i]);
  }
  return static_cast<uint8_t>(sum ^ kChecksumXor);
}

// Field tables: index = position in the record payload.
uint8_t fieldGet(const PpSettings& s, uint8_t index)
{
  switch (index) {
    case 0: {
      return s.animations ? 1 : 0;
    }
    case 1: {
      return s.pokegachi ? 1 : 0;
    }
    default: {
      return 0;
    }
  }
}

void fieldSet(PpSettings& s, uint8_t index, bool value)
{
  switch (index) {
    case 0: {
      s.animations = value;
      break;
    }
    case 1: {
      s.pokegachi = value;
      break;
    }
    default: {
      break;
    }
  }
}

}  // namespace

PpSettings ppSettingsDefaults()
{
  PpSettings s;
  s.animations = true;
  s.pokegachi = false;
  return s;
}

size_t ppSettingsPack(const PpSettings& s, uint8_t* out, size_t cap)
{
  size_t total = static_cast<size_t>(kFieldCount) + 3;
  if (out == nullptr || cap < total) {
    return 0;
  }
  out[0] = PP_SETTINGS_VERSION;
  out[1] = kFieldCount;
  for (uint8_t i = 0; i < kFieldCount; ++i) {
    out[2 + i] = fieldGet(s, i);
  }
  out[total - 1] = checksum(out, total - 1);
  return total;
}

bool ppSettingsUnpack(const uint8_t* in, size_t len, PpSettings& out)
{
  out = ppSettingsDefaults();
  if (in == nullptr || len < 3) {
    return false;
  }
  if (in[0] != PP_SETTINGS_VERSION) {
    return false;
  }
  size_t payload = in[1];
  if (payload + 3 > len) {
    return false;
  }
  if (in[2 + payload] != checksum(in, 2 + payload)) {
    return false;
  }
  for (uint8_t i = 0; i < kFieldCount && i < payload; ++i) {
    uint8_t raw = in[2 + i];
    if (raw <= 1) {
      fieldSet(out, i, raw == 1);
    }
  }
  return true;
}
