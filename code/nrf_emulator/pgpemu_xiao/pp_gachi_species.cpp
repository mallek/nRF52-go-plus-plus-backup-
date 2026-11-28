#include "pp_gachi_species.h"

static const PpGachiSpecies kSpecies[PP_GACHI_SPECIES_COUNT] = {
  {1,  "BULBASAUR",  0, 1, 3, 5,  2},
  {2,  "IVYSAUR",    0, 2, 3, 10, 3},
  {3,  "VENUSAUR",   0, 3, 3, 5,  0},
  {4,  "CHARMANDER", 1, 1, 3, 5,  5},
  {5,  "CHARMELEON", 1, 2, 3, 10, 6},
  {6,  "CHARIZARD",  1, 3, 3, 5,  0},
  {7,  "SQUIRTLE",   2, 1, 3, 5,  8},
  {8,  "WARTORTLE",  2, 2, 3, 10, 9},
  {9,  "BLASTOISE",  2, 3, 3, 5,  0},
  {25, "PIKACHU",    3, 1, 2, 8,  26},
  {26, "RAICHU",     3, 2, 2, 5,  0},
};

static const uint8_t kLineBase[PP_GACHI_LINE_COUNT] = {1, 4, 7, 25};
static const uint8_t kLineFinal[PP_GACHI_LINE_COUNT] = {3, 6, 9, 26};

const PpGachiSpecies& ppGachiSpeciesAt(uint8_t index)
{
  if (index >= PP_GACHI_SPECIES_COUNT) {
    index = PP_GACHI_SPECIES_COUNT - 1;
  }
  return kSpecies[index];
}

int8_t ppGachiSpeciesIndex(uint8_t dex)
{
  for (uint8_t i = 0; i < PP_GACHI_SPECIES_COUNT; ++i) {
    if (kSpecies[i].dex == dex) {
      return (int8_t)i;
    }
  }
  return -1;
}

const PpGachiSpecies* ppGachiSpeciesByDex(uint8_t dex)
{
  int8_t i = ppGachiSpeciesIndex(dex);
  return (i < 0) ? nullptr : &kSpecies[i];
}

uint8_t ppGachiLineBaseDex(uint8_t line)
{
  if (line >= PP_GACHI_LINE_COUNT) {
    line = PP_GACHI_LINE_COUNT - 1;
  }
  return kLineBase[line];
}

uint8_t ppGachiLineFinalDex(uint8_t line)
{
  if (line >= PP_GACHI_LINE_COUNT) {
    line = PP_GACHI_LINE_COUNT - 1;
  }
  return kLineFinal[line];
}
