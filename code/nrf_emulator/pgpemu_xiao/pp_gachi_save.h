#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pp_gachi_logic.h"

// PokeGachi save record. Pure: no Arduino, no flash access, no allocation.
// Compiled into the sketch and also by the host tests in tests/gachi/.
//
// Record: [0] version, [1..2] payload length N (little-endian), N payload bytes,
// then 2 bytes of Fletcher-16 over every byte before them (low byte first).
// Payload, multi-byte values little-endian: hasPet, petDex, petShiny, nutrition,
// happiness, experience, petBattlesWon (2), arrivalPending, berries, opponentCount,
// opponents (5), dexRaised (2), dexShiny (2), rng (4), sanctuaryCount, then
// sanctuaryCount records of dex, shiny, battlesWon (2).
//
// To add a field: append it to the payload end. Older records keep loading and the
// new field takes its fresh default.

static constexpr uint8_t PP_GACHI_SAVE_VERSION = 1;
static constexpr size_t PP_GACHI_SAVE_MAX = 256;

// Bytes written, or 0 when cap is too small.
size_t ppGachiPack(const PpGachiState& st, uint8_t* out, size_t cap);

// Always leaves `out` usable. A record that passes the checksum is still made safe
// (caps, roster, tracks). Returns false, with out a fresh game seeded by freshSeed,
// on a record that is too short, the wrong version, damaged, or holds an unknown pet.
bool ppGachiUnpack(const uint8_t* in, size_t len, PpGachiState& out, uint32_t freshSeed);
