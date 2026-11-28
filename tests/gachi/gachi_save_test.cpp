// Host tests for pp_gachi_save. Build + run from this folder (WSL or any g++):
//   g++ -std=c++17 -Wall -Wextra -Werror -I../../code/nrf_emulator/pgpemu_xiao
//       gachi_save_test.cpp ../../code/nrf_emulator/pgpemu_xiao/pp_gachi_save.cpp
//       ../../code/nrf_emulator/pgpemu_xiao/pp_gachi_logic.cpp
//       ../../code/nrf_emulator/pgpemu_xiao/pp_gachi_species.cpp -o t
//   ./t
// Or all host tests: bash tests/run.sh

#include <cstdio>
#include <cstring>

#include "pp_gachi_logic.h"
#include "pp_gachi_save.h"
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

static const uint32_t kFresh = 0x1234ABCDu;

static uint16_t fletcher16(const uint8_t* p, size_t n)
{
  uint16_t a = 0;
  uint16_t b = 0;
  for (size_t i = 0; i < n; ++i) {
    a = (uint16_t)((a + p[i]) % 255);
    b = (uint16_t)((b + a) % 255);
  }
  return (uint16_t)((b << 8) | a);
}

// Builds a record around a payload with a correct checksum.
static size_t seal(uint8_t version, const uint8_t* payload, size_t n, uint8_t* out)
{
  out[0] = version;
  out[1] = (uint8_t)(n & 0xFF);
  out[2] = (uint8_t)(n >> 8);
  std::memcpy(out + 3, payload, n);
  uint16_t c = fletcher16(out, 3 + n);
  out[3 + n] = (uint8_t)(c & 0xFF);
  out[4 + n] = (uint8_t)(c >> 8);
  return 5 + n;
}

static void refix(uint8_t* rec, size_t len)
{
  uint16_t c = fletcher16(rec, len - 2);
  rec[len - 2] = (uint8_t)(c & 0xFF);
  rec[len - 1] = (uint8_t)(c >> 8);
}

static bool sameState(const PpGachiState& a, const PpGachiState& b)
{
  if (a.hasPet != b.hasPet || a.petDex != b.petDex || a.petShiny != b.petShiny ||
      a.nutrition != b.nutrition || a.happiness != b.happiness ||
      a.experience != b.experience || a.petBattlesWon != b.petBattlesWon ||
      a.arrivalPending != b.arrivalPending || a.berries != b.berries ||
      a.opponentCount != b.opponentCount || a.dexRaised != b.dexRaised ||
      a.dexShiny != b.dexShiny || a.sanctuaryCount != b.sanctuaryCount || a.rng != b.rng) {
    return false;
  }
  for (uint8_t i = 0; i < PP_GACHI_OPPONENT_CAP; ++i) {
    if (a.opponents[i] != b.opponents[i]) {
      return false;
    }
  }
  for (uint8_t i = 0; i < a.sanctuaryCount; ++i) {
    if (a.sanctuary[i].dex != b.sanctuary[i].dex || a.sanctuary[i].shiny != b.sanctuary[i].shiny ||
        a.sanctuary[i].battlesWon != b.sanctuary[i].battlesWon) {
      return false;
    }
  }
  return true;
}

static bool isFresh(const PpGachiState& st)
{
  PpGachiState f;
  ppGachiInit(f, kFresh);
  return sameState(st, f);
}

// Everything the rest of the game assumes about a state.
static bool invariantsHold(const PpGachiState& st)
{
  if (st.rng == 0 || st.berries > PP_GACHI_BERRY_CAP || st.opponentCount > PP_GACHI_OPPONENT_CAP ||
      st.sanctuaryCount > PP_GACHI_SANCTUARY_CAP) {
    return false;
  }
  if (st.hasPet) {
    if (ppGachiSpeciesByDex(st.petDex) == nullptr) {
      return false;
    }
  } else if (st.nutrition != 0 || st.happiness != 0 || st.experience != 0) {
    return false;
  }
  uint8_t need = ppGachiNeed(st);
  uint8_t lowest = st.nutrition < st.experience ? st.nutrition : st.experience;
  if (st.nutrition > need || st.experience > need || st.happiness > need || st.happiness > lowest) {
    return false;
  }
  for (uint8_t i = 0; i < st.opponentCount; ++i) {
    if (ppGachiSpeciesByDex(st.opponents[i]) == nullptr) {
      return false;
    }
  }
  for (uint8_t i = 0; i < st.sanctuaryCount; ++i) {
    if (ppGachiSpeciesByDex(st.sanctuary[i].dex) == nullptr) {
      return false;
    }
  }
  uint16_t mask = (uint16_t)((1u << PP_GACHI_SPECIES_COUNT) - 1u);
  return (st.dexRaised & ~mask) == 0 && (st.dexShiny & ~mask) == 0;
}

static void fillFull(PpGachiState& st)
{
  ppGachiInit(st, 99);
  st.hasPet = true;
  st.petDex = 2;  // Ivysaur, stage 2
  st.petShiny = true;
  uint8_t need = ppGachiNeed(st);
  st.nutrition = need;
  st.experience = need;
  st.happiness = need;
  st.petBattlesWon = 300;
  st.arrivalPending = true;
  st.berries = PP_GACHI_BERRY_CAP;
  st.opponentCount = PP_GACHI_OPPONENT_CAP;
  for (uint8_t i = 0; i < PP_GACHI_OPPONENT_CAP; ++i) {
    st.opponents[i] = ppGachiSpeciesAt(i).dex;
  }
  st.dexRaised = 0x5A5;
  st.dexShiny = 0x105;
  st.sanctuaryCount = PP_GACHI_SANCTUARY_CAP;
  for (uint8_t i = 0; i < PP_GACHI_SANCTUARY_CAP; ++i) {
    st.sanctuary[i].dex = ppGachiSpeciesAt(i % PP_GACHI_SPECIES_COUNT).dex;
    st.sanctuary[i].shiny = (i % 3) == 0;
    st.sanctuary[i].battlesWon = (uint16_t)(1000 + i);
  }
  st.rng = 0xDEADBEEFu;
}

static void testRoundTripFresh()
{
  PpGachiState st;
  ppGachiInit(st, 5);
  uint8_t buf[PP_GACHI_SAVE_MAX];
  size_t n = ppGachiPack(st, buf, sizeof(buf));
  CHECK(n > 0);
  PpGachiState out;
  CHECK(ppGachiUnpack(buf, n, out, kFresh));
  CHECK(sameState(st, out));
}

static void testRoundTripFull()
{
  PpGachiState st;
  fillFull(st);
  uint8_t buf[PP_GACHI_SAVE_MAX];
  size_t n = ppGachiPack(st, buf, sizeof(buf));
  CHECK(n > 0);
  PpGachiState out;
  CHECK(ppGachiUnpack(buf, n, out, kFresh));
  CHECK(sameState(st, out));
  CHECK(out.sanctuaryCount == PP_GACHI_SANCTUARY_CAP);
}

static void testSizeWithinMax()
{
  PpGachiState st;
  fillFull(st);
  uint8_t buf[PP_GACHI_SAVE_MAX];
  size_t n = ppGachiPack(st, buf, sizeof(buf));
  CHECK(n > 0 && n <= PP_GACHI_SAVE_MAX);
}

static void testPackTooSmall()
{
  PpGachiState st;
  ppGachiInit(st, 5);
  uint8_t buf[10];
  CHECK(ppGachiPack(st, buf, sizeof(buf)) == 0);
  uint8_t full[PP_GACHI_SAVE_MAX];
  size_t n = ppGachiPack(st, full, sizeof(full));
  CHECK(ppGachiPack(st, full, n - 1) == 0);
  CHECK(ppGachiPack(st, full, n) == n);
}

static void testRejects()
{
  PpGachiState st;
  fillFull(st);
  uint8_t buf[PP_GACHI_SAVE_MAX];
  size_t n = ppGachiPack(st, buf, sizeof(buf));
  PpGachiState out;

  std::memset(&out, 0x77, sizeof(out));
  CHECK(!ppGachiUnpack(buf, 0, out, kFresh));
  CHECK(isFresh(out));

  std::memset(&out, 0x77, sizeof(out));
  CHECK(!ppGachiUnpack(nullptr, 0, out, kFresh));
  CHECK(isFresh(out));

  for (size_t cut = 1; cut <= 6; ++cut) {
    std::memset(&out, 0x77, sizeof(out));
    CHECK(!ppGachiUnpack(buf, n - cut, out, kFresh));
    CHECK(isFresh(out));
  }

  uint8_t bad[PP_GACHI_SAVE_MAX];
  std::memcpy(bad, buf, n);
  bad[n - 1] ^= 0x01;
  std::memset(&out, 0x77, sizeof(out));
  CHECK(!ppGachiUnpack(bad, n, out, kFresh));
  CHECK(isFresh(out));

  std::memcpy(bad, buf, n);
  bad[0] = PP_GACHI_SAVE_VERSION + 1;
  refix(bad, n);
  std::memset(&out, 0x77, sizeof(out));
  CHECK(!ppGachiUnpack(bad, n, out, kFresh));
  CHECK(isFresh(out));

  // N claims more than the data holds, checksum otherwise valid for the bytes given.
  std::memcpy(bad, buf, n);
  bad[1] = (uint8_t)(bad[1] + 20);
  refix(bad, n);
  std::memset(&out, 0x77, sizeof(out));
  CHECK(!ppGachiUnpack(bad, n, out, kFresh));
  CHECK(isFresh(out));

  // N huge.
  std::memcpy(bad, buf, n);
  bad[1] = 0xFF;
  bad[2] = 0xFF;
  refix(bad, n);
  std::memset(&out, 0x77, sizeof(out));
  CHECK(!ppGachiUnpack(bad, n, out, kFresh));
  CHECK(isFresh(out));
}

static void testUnknownPetRejected()
{
  PpGachiState st;
  fillFull(st);
  st.petDex = 150;
  uint8_t buf[PP_GACHI_SAVE_MAX];
  size_t n = ppGachiPack(st, buf, sizeof(buf));
  CHECK(n > 0);
  PpGachiState out;
  std::memset(&out, 0x77, sizeof(out));
  CHECK(!ppGachiUnpack(buf, n, out, kFresh));
  CHECK(isFresh(out));
}

// Payload layout offsets (see the header).
enum {
  O_HASPET = 0, O_DEX = 1, O_SHINY = 2, O_NUT = 3, O_HAP = 4, O_EXP = 5, O_WON = 6, O_ARR = 8,
  O_BERRIES = 9, O_OPPCOUNT = 10, O_OPP = 11, O_RAISED = 16, O_DSHINY = 18, O_RNG = 20,
  O_SANCCOUNT = 24, O_SANC = 25
};

static void testOutOfRangeMadeSafe()
{
  PpGachiState st;
  fillFull(st);
  uint8_t buf[PP_GACHI_SAVE_MAX];
  size_t n = ppGachiPack(st, buf, sizeof(buf));
  uint8_t* p = buf + 3;
  p[O_BERRIES] = 99;
  p[O_OPPCOUNT] = 9;
  p[O_NUT] = 200;
  p[O_HAP] = 200;
  p[O_EXP] = 3;
  p[O_OPP + 1] = 77;
  p[O_SANCCOUNT] = 60;
  p[O_RNG] = p[O_RNG + 1] = p[O_RNG + 2] = p[O_RNG + 3] = 0;
  p[O_RAISED + 1] = 0xFF;
  p[O_DSHINY + 1] = 0xFF;
  refix(buf, n);
  PpGachiState out;
  CHECK(ppGachiUnpack(buf, n, out, kFresh));
  CHECK(out.berries == PP_GACHI_BERRY_CAP);
  CHECK(out.opponentCount <= PP_GACHI_OPPONENT_CAP);
  CHECK(out.nutrition == ppGachiNeed(out));
  CHECK(out.happiness <= out.experience);
  CHECK(out.sanctuaryCount <= PP_GACHI_SANCTUARY_CAP);
  CHECK(out.rng == kFresh);
  CHECK(invariantsHold(out));
  CHECK(out.opponentCount == 4);  // one of five dropped for dex 77
}

static void testOlderShortRecord()
{
  uint8_t payload[10] = {1, 1, 0, 2, 1, 1, 0, 0, 0, 7};  // ends after berries
  uint8_t rec[32];
  size_t n = seal(PP_GACHI_SAVE_VERSION, payload, sizeof(payload), rec);
  PpGachiState out;
  CHECK(ppGachiUnpack(rec, n, out, kFresh));
  CHECK(out.hasPet && out.petDex == 1);
  CHECK(out.berries == 7);
  CHECK(out.opponentCount == 0);
  CHECK(out.dexRaised == 0 && out.dexShiny == 0);
  CHECK(out.sanctuaryCount == 0);
  CHECK(out.rng == kFresh);
  CHECK(invariantsHold(out));
}

static void testSanctuaryCountPastData()
{
  PpGachiState st;
  ppGachiInit(st, 5);
  st.sanctuaryCount = 2;
  st.sanctuary[0] = {1, false, 3};
  st.sanctuary[1] = {4, true, 9};
  uint8_t buf[PP_GACHI_SAVE_MAX];
  size_t n = ppGachiPack(st, buf, sizeof(buf));
  buf[3 + O_SANCCOUNT] = 5;
  refix(buf, n);
  PpGachiState out;
  CHECK(ppGachiUnpack(buf, n, out, kFresh));
  CHECK(out.sanctuaryCount == 2);
  CHECK(out.sanctuary[1].dex == 4 && out.sanctuary[1].shiny && out.sanctuary[1].battlesWon == 9);
  CHECK(invariantsHold(out));
}

static void testSanctuaryBadDexDropped()
{
  PpGachiState st;
  ppGachiInit(st, 5);
  st.sanctuaryCount = 3;
  st.sanctuary[0] = {1, false, 3};
  st.sanctuary[1] = {4, true, 9};
  st.sanctuary[2] = {7, false, 1};
  uint8_t buf[PP_GACHI_SAVE_MAX];
  size_t n = ppGachiPack(st, buf, sizeof(buf));
  buf[3 + O_SANC + 4] = 200;  // middle record dex
  refix(buf, n);
  PpGachiState out;
  CHECK(ppGachiUnpack(buf, n, out, kFresh));
  CHECK(out.sanctuaryCount == 2);
  CHECK(out.sanctuary[0].dex == 1 && out.sanctuary[1].dex == 7);
  CHECK(invariantsHold(out));
}

// Flip every byte of a valid record every way, re-checksum, unpack: the result must
// always satisfy the invariants and a short or inflated length must never read past len.
static void testFuzzFlips()
{
  PpGachiState st;
  fillFull(st);
  uint8_t base[PP_GACHI_SAVE_MAX];
  size_t n = ppGachiPack(st, base, sizeof(base));
  CHECK(n > 0);
  int accepted = 0;
  for (size_t i = 0; i < n - 2; ++i) {
    for (int v = 0; v < 256; ++v) {
      // Exact-size buffer so an over-read is visible to sanitizers; stack copy is enough here.
      uint8_t rec[PP_GACHI_SAVE_MAX];
      std::memcpy(rec, base, n);
      rec[i] = (uint8_t)v;
      refix(rec, n);
      PpGachiState out;
      std::memset(&out, 0x77, sizeof(out));
      bool ok = ppGachiUnpack(rec, n, out, kFresh);
      if (ok) {
        ++accepted;
      } else {
        CHECK(isFresh(out));
      }
      CHECK(invariantsHold(out));
    }
  }
  CHECK(accepted > 0);
  // Every prefix length of the valid record, and of a re-sealed truncated payload.
  for (size_t len = 0; len <= n; ++len) {
    PpGachiState out;
    ppGachiUnpack(base, len, out, kFresh);
    CHECK(invariantsHold(out));
  }
  for (size_t m = 0; m < n - 5; ++m) {
    uint8_t rec[PP_GACHI_SAVE_MAX];
    size_t len = seal(PP_GACHI_SAVE_VERSION, base + 3, m, rec);
    PpGachiState out;
    ppGachiUnpack(rec, len, out, kFresh);
    CHECK(invariantsHold(out));
  }
}

int main()
{
  testRoundTripFresh();
  testRoundTripFull();
  testSizeWithinMax();
  testPackTooSmall();
  testRejects();
  testUnknownPetRejected();
  testOutOfRangeMadeSafe();
  testOlderShortRecord();
  testSanctuaryCountPastData();
  testSanctuaryBadDexDropped();
  testFuzzFlips();
  if (failures == 0) {
    std::printf("gachi_save: all passed\n");
    return 0;
  }
  std::printf("gachi_save: %d failure(s)\n", failures);
  return 1;
}
