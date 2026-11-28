#pragma once

#include <stdint.h>

// PokeGachi species table: the roster, its evolution lines and care points.
// Pure data and lookups: no Arduino, no allocation. The table is static const.
// Compiled into the sketch and also by the host tests in tests/gachi/.

static constexpr uint8_t PP_GACHI_SPECIES_COUNT = 11;
static constexpr uint8_t PP_GACHI_LINE_COUNT = 4;
static constexpr uint8_t PP_GACHI_STARTER_LINES = 3;     // lines 0..2 are the starters

struct PpGachiSpecies {
  uint8_t dex;         // national dex number, the species id everywhere
  const char* name;    // upper case, 7-bit ASCII
  uint8_t line;        // 0 Bulbasaur, 1 Charmander, 2 Squirtle, 3 Pikachu
  uint8_t stage;       // 1-based position in its line
  uint8_t stages;      // forms in its line
  uint8_t carePoints;  // needed on each track at this stage
  uint8_t evolvesTo;   // dex of the next form, 0 for a final form
};

const PpGachiSpecies& ppGachiSpeciesAt(uint8_t index);   // roster order = dex order; index clamped
int8_t ppGachiSpeciesIndex(uint8_t dex);                 // -1 when not in the roster
const PpGachiSpecies* ppGachiSpeciesByDex(uint8_t dex);  // nullptr when not in the roster
uint8_t ppGachiLineBaseDex(uint8_t line);                // first form of a line; line clamped
uint8_t ppGachiLineFinalDex(uint8_t line);
