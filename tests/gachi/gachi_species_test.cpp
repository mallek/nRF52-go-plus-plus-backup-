// Host tests for pp_gachi_species. Build + run from this folder (WSL or any g++):
//   g++ -std=c++17 -Wall -Wextra -Werror -I../../code/nrf_emulator/pgpemu_xiao
//       gachi_species_test.cpp ../../code/nrf_emulator/pgpemu_xiao/pp_gachi_species.cpp -o t
//   ./t
// Or all host tests: bash tests/run.sh

#include <cstdio>
#include <cstring>

#include "pp_gachi_species.h"
#include "pp_gachi_tuning.h"

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

static void testRosterShape()
{
  CHECK(PP_GACHI_SPECIES_COUNT == 11);
  for (uint8_t i = 0; i < PP_GACHI_SPECIES_COUNT; ++i) {
    const PpGachiSpecies& s = ppGachiSpeciesAt(i);
    CHECK(s.name != nullptr);
    CHECK(s.name != nullptr && s.name[0] != '\0');
    for (const char* p = s.name; p != nullptr && *p != '\0'; ++p) {
      CHECK(*p >= 'A' && *p <= 'Z');
    }
    if (i > 0) {
      CHECK(ppGachiSpeciesAt(i).dex > ppGachiSpeciesAt(i - 1).dex);
    }
  }
}

static void testLines()
{
  const uint8_t base[4] = {1, 4, 7, 25};
  const uint8_t fin[4] = {3, 6, 9, 26};
  for (uint8_t l = 0; l < PP_GACHI_LINE_COUNT; ++l) {
    CHECK(ppGachiLineBaseDex(l) == base[l]);
    CHECK(ppGachiLineFinalDex(l) == fin[l]);
  }
  CHECK(ppGachiLineBaseDex(200) == 25);
  CHECK(ppGachiLineFinalDex(200) == 26);
}

static void testCarePoints()
{
  const uint8_t dex[11] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 25, 26};
  const uint8_t care[11] = {5, 10, 5, 5, 10, 5, 5, 10, 5, 8, 5};
  for (uint8_t i = 0; i < 11; ++i) {
    const PpGachiSpecies* s = ppGachiSpeciesByDex(dex[i]);
    CHECK(s != nullptr);
    if (s != nullptr) {
      CHECK(s->carePoints == care[i]);
    }
  }
}

static void testEvolutionChain()
{
  const uint8_t next[11] = {2, 3, 0, 5, 6, 0, 8, 9, 0, 26, 0};
  const uint8_t dex[11] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 25, 26};
  for (uint8_t i = 0; i < 11; ++i) {
    const PpGachiSpecies& s = ppGachiSpeciesAt(i);
    CHECK(s.dex == dex[i]);
    CHECK(s.evolvesTo == next[i]);
    CHECK(s.stage >= 1 && s.stage <= s.stages);
    CHECK((s.evolvesTo == 0) == (s.stage == s.stages));
    if (s.evolvesTo != 0) {
      const PpGachiSpecies* n = ppGachiSpeciesByDex(s.evolvesTo);
      CHECK(n != nullptr);
      if (n != nullptr) {
        CHECK(n->line == s.line);
        CHECK(n->stage == s.stage + 1);
        CHECK(n->stages == s.stages);
      }
    }
  }
  CHECK(PP_GACHI_STARTER_LINES == 3);
}

static void testLookups()
{
  CHECK(ppGachiSpeciesIndex(25) == 9);
  CHECK(ppGachiSpeciesIndex(1) == 0);
  CHECK(ppGachiSpeciesIndex(10) == -1);
  CHECK(ppGachiSpeciesByDex(0) == nullptr);
  CHECK(ppGachiSpeciesByDex(10) == nullptr);
  CHECK(ppGachiSpeciesByDex(26) != nullptr);
  CHECK(ppGachiSpeciesAt(200).dex == 26);
}

int main()
{
  testRosterShape();
  testLines();
  testCarePoints();
  testEvolutionChain();
  testLookups();
  if (failures == 0) {
    std::printf("gachi_species: all passed\n");
    return 0;
  }
  std::printf("gachi_species: %d failure(s)\n", failures);
  return 1;
}
