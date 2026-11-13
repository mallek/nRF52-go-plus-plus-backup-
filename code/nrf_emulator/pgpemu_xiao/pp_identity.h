#pragma once

#include <stdint.h>

// One Plus+ identity: the donor's public BLE address plus the material read from its
// flash. The table is generated (pp_identities.generated.h) and never committed.
struct PpIdentity {
  const char* name;
  uint8_t addrLe[6];
  uint8_t certBlock[256];
  uint8_t selector3[16];
  uint8_t selector4[16];
  uint8_t selector6[10];
};

// Live identity. ppIdentityBegin() fills these from the selected table entry before
// the BLE stack starts; the protocol code reads them by name.
extern uint8_t REAL_PLUSPLUS_ADDR_LE[6];
extern uint8_t PLUSPLUS_FACTORY_CERT_BLOCK[256];
extern uint8_t PLUSPLUS_FACTORY_SELECTOR3[16];
extern uint8_t PLUSPLUS_FACTORY_SELECTOR4[16];
extern uint8_t PLUSPLUS_FACTORY_SELECTOR6[10];

// Mounts InternalFS, reads the saved selection (by MAC) and loads it. When the saved
// selection is missing or not in this build's table, it falls back to entry 0 and
// clears the pair state, since that state belongs to some other identity.
void ppIdentityBegin();

uint8_t ppIdentityCount();
uint8_t ppIdentityActive();
const char* ppIdentityName(uint8_t index);

// Saves the new selection, clears the pair state and reboots. Does not return.
void ppIdentitySwitchTo(uint8_t index);
