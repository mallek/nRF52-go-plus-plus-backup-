#include "pp_gachi_logic.h"

#include <string.h>

#include "pp_gachi_species.h"

static const uint32_t RNG_FALLBACK_SEED = 0x2545F491u;

static uint8_t pickLine(PpGachiState& st)
{
  if (st.dexRaised == 0) {
    return (uint8_t)ppGachiRandom(st, PP_GACHI_STARTER_LINES);
  }
  uint8_t open[PP_GACHI_LINE_COUNT];
  uint8_t openCount = 0;
  for (uint8_t line = 0; line < PP_GACHI_LINE_COUNT; ++line) {
    if (!ppGachiLineComplete(st, line)) {
      open[openCount] = line;
      ++openCount;
    }
  }
  if (openCount == 0) {
    return (uint8_t)ppGachiRandom(st, PP_GACHI_LINE_COUNT);
  }
  return open[ppGachiRandom(st, openCount)];
}

static void deliverPet(PpGachiState& st)
{
  uint8_t line = pickLine(st);
  bool shiny = ppGachiRandom(st, PP_GACHI_SHINY_ODDS) == 0;
  st.hasPet = true;
  st.petDex = ppGachiLineBaseDex(line);
  st.petShiny = shiny;
  st.nutrition = 0;
  st.happiness = 0;
  st.experience = 0;
  st.petBattlesWon = 0;
  st.arrivalPending = true;
  int8_t idx = ppGachiSpeciesIndex(st.petDex);
  if (idx >= 0) {
    st.dexRaised = (uint16_t)(st.dexRaised | (1u << idx));
    if (shiny) {
      st.dexShiny = (uint16_t)(st.dexShiny | (1u << idx));
    }
  }
}

void ppGachiInit(PpGachiState& st, uint32_t seed)
{
  memset(&st, 0, sizeof(st));
  st.rng = (seed == 0) ? RNG_FALLBACK_SEED : seed;
}

uint32_t ppGachiRandom(PpGachiState& st, uint32_t n)
{
  if (st.rng == 0) {
    st.rng = RNG_FALLBACK_SEED;
  }
  uint32_t x = st.rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  st.rng = x;
  if (n == 0) {
    return 0;
  }
  return x % n;
}

PpGachiGrant ppGachiOnEvent(PpGachiState& st, PpFeedbackEvent event, bool enabled)
{
  if (!enabled) {
    return PP_GACHI_GRANT_NONE;
  }
  if (event == PP_FEEDBACK_POKESTOP_SPUN) {
    if (st.berries < PP_GACHI_BERRY_CAP) {
      ++st.berries;
      return PP_GACHI_GRANT_BERRY;
    }
    return PP_GACHI_GRANT_NONE;
  }
  if (event == PP_FEEDBACK_CAUGHT) {
    if (!st.hasPet) {
      deliverPet(st);
      return PP_GACHI_GRANT_PET;
    }
    if (st.opponentCount < PP_GACHI_OPPONENT_CAP) {
      st.opponents[st.opponentCount] = ppGachiSpeciesAt((uint8_t)ppGachiRandom(st, PP_GACHI_SPECIES_COUNT)).dex;
      ++st.opponentCount;
      return PP_GACHI_GRANT_OPPONENT;
    }
  }
  return PP_GACHI_GRANT_NONE;
}

bool ppGachiDexRaised(const PpGachiState& st, uint8_t rosterIndex)
{
  return rosterIndex < PP_GACHI_SPECIES_COUNT && ((st.dexRaised >> rosterIndex) & 1u) != 0;
}

bool ppGachiDexShiny(const PpGachiState& st, uint8_t rosterIndex)
{
  return rosterIndex < PP_GACHI_SPECIES_COUNT && ((st.dexShiny >> rosterIndex) & 1u) != 0;
}

uint8_t ppGachiDexRaisedCount(const PpGachiState& st)
{
  uint8_t count = 0;
  for (uint8_t i = 0; i < PP_GACHI_SPECIES_COUNT; ++i) {
    if (ppGachiDexRaised(st, i)) {
      ++count;
    }
  }
  return count;
}

bool ppGachiLineComplete(const PpGachiState& st, uint8_t line)
{
  int8_t idx = ppGachiSpeciesIndex(ppGachiLineFinalDex(line));
  return idx >= 0 && ppGachiDexRaised(st, (uint8_t)idx);
}

uint8_t ppGachiNeed(const PpGachiState& st)
{
  if (!st.hasPet) {
    return 0;
  }
  const PpGachiSpecies* sp = ppGachiSpeciesByDex(st.petDex);
  return sp != nullptr ? sp->carePoints : 0;
}

bool ppGachiReady(const PpGachiState& st)
{
  if (!st.hasPet) {
    return false;
  }
  uint8_t need = ppGachiNeed(st);
  return st.nutrition >= need && st.happiness >= need && st.experience >= need;
}

bool ppGachiIsFinalForm(const PpGachiState& st)
{
  const PpGachiSpecies* sp = st.hasPet ? ppGachiSpeciesByDex(st.petDex) : nullptr;
  return sp != nullptr && sp->evolvesTo == 0;
}

PpGachiResult ppGachiFeed(PpGachiState& st)
{
  if (!st.hasPet) {
    return PP_GACHI_NO_PET;
  }
  if (st.nutrition >= ppGachiNeed(st)) {
    return PP_GACHI_NUTRITION_FULL;
  }
  if (st.berries == 0) {
    return PP_GACHI_NO_BERRIES;
  }
  --st.berries;
  ++st.nutrition;
  return PP_GACHI_OK;
}

PpGachiResult ppGachiPlay(PpGachiState& st)
{
  if (!st.hasPet) {
    return PP_GACHI_NO_PET;
  }
  if (st.happiness >= ppGachiNeed(st)) {
    return PP_GACHI_PLAY_FULL;
  }
  uint8_t limit = st.nutrition < st.experience ? st.nutrition : st.experience;
  if (st.happiness < limit) {
    ++st.happiness;
    return PP_GACHI_OK;
  }
  if (st.nutrition < st.experience) {
    return PP_GACHI_PLAY_NEED_FOOD;
  }
  if (st.experience < st.nutrition) {
    return PP_GACHI_PLAY_NEED_BATTLE;
  }
  return PP_GACHI_PLAY_NEED_BOTH;
}

PpGachiResult ppGachiBattleResult(PpGachiState& st, bool won)
{
  if (!st.hasPet) {
    return PP_GACHI_NO_PET;
  }
  if (st.opponentCount == 0) {
    return PP_GACHI_NO_OPPONENT;
  }
  if (!won) {
    return PP_GACHI_OK;
  }
  for (uint8_t i = 1; i < st.opponentCount; ++i) {
    st.opponents[i - 1] = st.opponents[i];
  }
  --st.opponentCount;
  if (st.petBattlesWon < 65535) {
    ++st.petBattlesWon;
  }
  if (st.experience < ppGachiNeed(st)) {
    ++st.experience;
  }
  return PP_GACHI_OK;
}

PpGachiResult ppGachiAdvance(PpGachiState& st)
{
  if (!st.hasPet) {
    return PP_GACHI_NO_PET;
  }
  if (!ppGachiReady(st)) {
    return PP_GACHI_NOT_READY;
  }
  const PpGachiSpecies* sp = ppGachiSpeciesByDex(st.petDex);
  if (sp != nullptr && sp->evolvesTo != 0) {
    st.petDex = sp->evolvesTo;
    st.nutrition = 0;
    st.happiness = 0;
    st.experience = 0;
    int8_t idx = ppGachiSpeciesIndex(st.petDex);
    if (idx >= 0) {
      st.dexRaised = (uint16_t)(st.dexRaised | (1u << idx));
      if (st.petShiny) {
        st.dexShiny = (uint16_t)(st.dexShiny | (1u << idx));
      }
    }
    return PP_GACHI_EVOLVED;
  }
  if (st.sanctuaryCount >= PP_GACHI_SANCTUARY_CAP) {
    for (uint8_t i = 1; i < PP_GACHI_SANCTUARY_CAP; ++i) {
      st.sanctuary[i - 1] = st.sanctuary[i];
    }
    st.sanctuaryCount = PP_GACHI_SANCTUARY_CAP - 1;
  }
  PpGachiRetired& rec = st.sanctuary[st.sanctuaryCount];
  rec.dex = st.petDex;
  rec.shiny = st.petShiny;
  rec.battlesWon = st.petBattlesWon;
  ++st.sanctuaryCount;
  st.hasPet = false;
  st.nutrition = 0;
  st.happiness = 0;
  st.experience = 0;
  return PP_GACHI_RETIRED;
}

void ppGachiReset(PpGachiState& st)
{
  uint32_t rng = st.rng;
  ppGachiInit(st, rng);
}
