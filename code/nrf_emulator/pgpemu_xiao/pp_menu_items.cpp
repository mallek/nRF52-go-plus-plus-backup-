#include "pp_menu_items.h"

#include <stdio.h>

#include "pp_gachi.h"
#include "pp_gachi_species.h"
#include "pp_identity.h"
#include "pp_settings.h"

namespace {

// Provider ids for the rows of the pages.
enum : uint8_t {
  kIdIdentity = 0,
  kIdAnimations,
  kIdPokegachi,
  kIdScreenSleep,
  kIdPokedex,
  kIdSanctuary,
  kIdEnabled,
  kIdReset
};

static constexpr uint8_t kPageIdentity = 1;
static constexpr uint8_t kPagePokegachi = 2;
static constexpr uint8_t kPagePokedex = 3;
static constexpr uint8_t kPageSanctuary = 4;
static constexpr uint8_t kPageReset = 5;

// A view holds the text pointers of every visible row at once, so each formatted
// text needs its own slot until the next view is built. One view needs at most
// PP_MENU_MAX_VISIBLE slots; the ring is twice that plus slack.
static constexpr uint8_t kTextLen = 20;
static constexpr uint8_t kRingSize = 2 * (PP_MENU_MAX_VISIBLE + 1);
static char gRing[kRingSize][kTextLen];
static uint8_t gRingNext = 0;

static char* ringSlot()
{
  char* slot = gRing[gRingNext];
  gRingNext = static_cast<uint8_t>((gRingNext + 1) % kRingSize);
  slot[0] = '\0';
  return slot;
}

// One multi-choice setting: its texts in step order, and how to read and write
// the current choice.
struct ChoiceSetting {
  uint8_t id;
  uint8_t count;
  const char* const* texts;
  uint8_t (*get)();
  void (*set)(uint8_t n);
};

static const char* const kOnOff[] = { "ON", "OFF" };
static const char* const kOffOn[] = { "OFF", "ON" };
// Same order as the times in pp_settings_logic.cpp.
static const char* const kScreenSleep[PP_SCREEN_SLEEP_COUNT] = { "10s", "30s", "1m", "2m" };

static uint8_t getAnimations()
{
  return ppSettings().animations ? 0 : 1;
}

static void setAnimations(uint8_t n)
{
  ppSettingsSetAnimations(n == 0);
}

static uint8_t getEnabled()
{
  return ppGachiEnabled() ? 1 : 0;
}

static void setEnabled(uint8_t n)
{
  ppGachiSetEnabled(n == 1);
}

static uint8_t getScreenSleep()
{
  return ppSettings().screenSleep;
}

static void setScreenSleep(uint8_t n)
{
  ppSettingsSetScreenSleep(n);
}

static const ChoiceSetting kChoices[] = {
  { kIdAnimations, 2, kOnOff, getAnimations, setAnimations },
  { kIdEnabled, 2, kOffOn, getEnabled, setEnabled },
  { kIdScreenSleep, PP_SCREEN_SLEEP_COUNT, kScreenSleep, getScreenSleep, setScreenSleep },
};

static const ChoiceSetting* findChoice(uint8_t id)
{
  for (const ChoiceSetting& c : kChoices) {
    if (c.id == id) {
      return &c;
    }
  }
  return nullptr;
}

static uint8_t choiceCount(uint8_t id)
{
  const ChoiceSetting* c = findChoice(id);
  return c != nullptr ? c->count : 0;
}

static uint8_t choiceCurrent(uint8_t id)
{
  const ChoiceSetting* c = findChoice(id);
  return c != nullptr ? c->get() : 0;
}

static const char* choiceText(uint8_t id, uint8_t n)
{
  const ChoiceSetting* c = findChoice(id);
  if (c == nullptr || n >= c->count) {
    return "";
  }
  return c->texts[n];
}

static void choiceSet(uint8_t id, uint8_t n)
{
  const ChoiceSetting* c = findChoice(id);
  if (c != nullptr && n < c->count) {
    c->set(n);
  }
}

// "off" with PokeGachi off, "no pet" with it on and no pet, else nullptr.
static const char* offOrNoPet()
{
  if (!ppGachiEnabled()) {
    return "off";
  }
  if (!ppGachiState().hasPet) {
    return "no pet";
  }
  return nullptr;
}

static const char* countText(uint8_t n, const char* suffix)
{
  char* t = ringSlot();
  snprintf(t, kTextLen, "%u %s", static_cast<unsigned>(n), suffix);
  return t;
}

static const char* valueText(uint8_t id)
{
  const PpGachiState& g = ppGachiState();
  const char* gate;
  switch (id) {
    case kIdIdentity:
      return ppIdentityName(ppIdentityActive());
    case kIdPokegachi:
      return ppGachiEnabled() ? "ON" : "OFF";
    case PP_MENU_ACTION_GACHI_FEED:
      gate = offOrNoPet();
      return gate != nullptr ? gate : countText(g.berries, "berries");
    case PP_MENU_ACTION_GACHI_PLAY:
      if (!ppGachiEnabled()) {
        return "";
      }
      return g.hasPet ? "" : "no pet";
    case PP_MENU_ACTION_GACHI_BATTLE:
      gate = offOrNoPet();
      return gate != nullptr ? gate : countText(g.opponentCount, "waiting");
    case PP_MENU_ACTION_GACHI_ADVANCE:
      gate = offOrNoPet();
      if (gate != nullptr) {
        return gate;
      }
      return ppGachiReady(g) ? "READY" : "not yet";
    case kIdPokedex: {
      char* t = ringSlot();
      snprintf(t, kTextLen, "%u/%u", static_cast<unsigned>(ppGachiDexRaisedCount(g)),
               static_cast<unsigned>(PP_GACHI_SPECIES_COUNT));
      return t;
    }
    case kIdSanctuary: {
      char* t = ringSlot();
      snprintf(t, kTextLen, "%u", static_cast<unsigned>(g.sanctuaryCount));
      return t;
    }
    default:
      return "";
  }
}

// Only the Evolve row changes its label: a final form retires.
static const char* labelText(uint8_t id)
{
  if (id == PP_MENU_ACTION_GACHI_ADVANCE) {
    const PpGachiState& g = ppGachiState();
    if (g.hasPet && ppGachiIsFinalForm(g)) {
      return "Retire";
    }
  }
  return nullptr;
}

static uint8_t listCount(uint8_t listId)
{
  switch (listId) {
    case PP_MENU_ACTION_IDENTITY:
      return ppIdentityCount();
    case PP_MENU_LIST_POKEDEX:
      return PP_GACHI_SPECIES_COUNT;
    case PP_MENU_LIST_SANCTUARY:
      return ppGachiState().sanctuaryCount;
    default:
      return 0;
  }
}

static const char* pokedexText(uint8_t n)
{
  const PpGachiState& g = ppGachiState();
  if (!ppGachiDexRaised(g, n)) {
    return "???";
  }
  char* t = ringSlot();
  snprintf(t, kTextLen, "%s%s", ppGachiSpeciesAt(n).name, ppGachiDexShiny(g, n) ? " *" : "");
  return t;
}

static const char* sanctuaryText(uint8_t n)
{
  const PpGachiState& g = ppGachiState();
  if (n >= g.sanctuaryCount) {
    return "";
  }
  const PpGachiRetired& r = g.sanctuary[g.sanctuaryCount - 1 - n];  // newest first
  const PpGachiSpecies* sp = ppGachiSpeciesByDex(r.dex);
  char* t = ringSlot();
  snprintf(t, kTextLen, "%s %uW%s", sp != nullptr ? sp->name : "?", static_cast<unsigned>(r.battlesWon),
           r.shiny ? " *" : "");
  return t;
}

static const char* listText(uint8_t listId, uint8_t n)
{
  switch (listId) {
    case PP_MENU_ACTION_IDENTITY:
      return n < ppIdentityCount() ? ppIdentityName(n) : "";
    case PP_MENU_LIST_POKEDEX:
      return n < PP_GACHI_SPECIES_COUNT ? pokedexText(n) : "";
    case PP_MENU_LIST_SANCTUARY:
      return sanctuaryText(n);
    default:
      return "";
  }
}

static uint8_t listActive(uint8_t listId)
{
  return listId == PP_MENU_ACTION_IDENTITY ? ppIdentityActive() : PP_MENU_NONE;
}

static const PpMenuProvider kProvider = { choiceCount, choiceCurrent, choiceText, choiceSet,
                                          valueText,   listCount,     listText,   listActive,
                                          labelText };

static const PpMenuItem kTopItems[] = {
  { "Identity", PP_MENU_SUBMENU, kIdIdentity, kPageIdentity },
  { "Animations", PP_MENU_CHOICE, kIdAnimations, 0 },
  { "Screen sleep", PP_MENU_CHOICE, kIdScreenSleep, 0 },
  { "PokeGachi", PP_MENU_SUBMENU, kIdPokegachi, kPagePokegachi },
};

static const PpMenuItem kGachiItems[] = {
  { "Feed", PP_MENU_ACTION, PP_MENU_ACTION_GACHI_FEED, 0 },
  { "Play", PP_MENU_ACTION, PP_MENU_ACTION_GACHI_PLAY, 0 },
  { "Battle", PP_MENU_ACTION, PP_MENU_ACTION_GACHI_BATTLE, 0 },
  { "Evolve", PP_MENU_ACTION, PP_MENU_ACTION_GACHI_ADVANCE, 0 },
  { "Pokedex", PP_MENU_SUBMENU, kIdPokedex, kPagePokedex },
  { "Sanctuary", PP_MENU_SUBMENU, kIdSanctuary, kPageSanctuary },
  { "Enabled", PP_MENU_CHOICE, kIdEnabled, 0 },
  { "Reset game", PP_MENU_SUBMENU, kIdReset, kPageReset },
};

// The safe choice is first, so the cursor opens on it.
static const PpMenuItem kResetItems[] = {
  { "No, keep it", PP_MENU_ACTION, PP_MENU_ACTION_GACHI_RESET_NO, 0 },
  { "Yes, wipe", PP_MENU_ACTION, PP_MENU_ACTION_GACHI_RESET_YES, 0 },
};

static const PpMenuPage kPages[] = {
  { "SETTINGS", kTopItems, sizeof(kTopItems) / sizeof(kTopItems[0]), 0 },
  { "IDENTITY", nullptr, 0, PP_MENU_ACTION_IDENTITY },
  { "POKEGACHI", kGachiItems, sizeof(kGachiItems) / sizeof(kGachiItems[0]), 0 },
  { "POKEDEX", nullptr, 0, PP_MENU_LIST_POKEDEX },
  { "SANCTUARY", nullptr, 0, PP_MENU_LIST_SANCTUARY },
  { "RESET GAME?", kResetItems, sizeof(kResetItems) / sizeof(kResetItems[0]), 0 },
};

static const PpMenu kMenu = { kPages, sizeof(kPages) / sizeof(kPages[0]), &kProvider, "A:select", "B:next" };

}  // namespace

const PpMenu& ppSettingsMenu()
{
  return kMenu;
}

uint8_t ppMenuPagePokegachi()
{
  return kPagePokegachi;
}
