#include "pp_menu_items.h"

#include "pp_identity.h"
#include "pp_settings.h"

namespace {

// Provider ids for the rows of the top page.
enum : uint8_t { kIdIdentity = 0, kIdAnimations, kIdPokegachi, kIdScreenSleep };

static constexpr uint8_t kPageIdentity = 1;

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

static uint8_t getPokegachi()
{
  return ppSettings().pokegachi ? 1 : 0;
}

static void setPokegachi(uint8_t n)
{
  ppSettingsSetPokegachi(n == 1);
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
  { kIdPokegachi, 2, kOffOn, getPokegachi, setPokegachi },
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

static const char* valueText(uint8_t id)
{
  if (id == kIdIdentity) {
    return ppIdentityName(ppIdentityActive());
  }
  return "";
}

static uint8_t listCount(uint8_t listId)
{
  return listId == PP_MENU_ACTION_IDENTITY ? ppIdentityCount() : 0;
}

static const char* listText(uint8_t listId, uint8_t n)
{
  if (listId != PP_MENU_ACTION_IDENTITY || n >= ppIdentityCount()) {
    return "";
  }
  return ppIdentityName(n);
}

static uint8_t listActive(uint8_t listId)
{
  return listId == PP_MENU_ACTION_IDENTITY ? ppIdentityActive() : PP_MENU_NONE;
}

static const PpMenuProvider kProvider = { choiceCount, choiceCurrent, choiceText, choiceSet,
                                          valueText,   listCount,     listText,   listActive,
                                          nullptr };

static const PpMenuItem kTopItems[] = {
  { "Identity", PP_MENU_SUBMENU, kIdIdentity, kPageIdentity },
  { "Animations", PP_MENU_CHOICE, kIdAnimations, 0 },
  { "Screen sleep", PP_MENU_CHOICE, kIdScreenSleep, 0 },
  { "PokeGachi", PP_MENU_CHOICE, kIdPokegachi, 0 },
};

static const PpMenuPage kPages[] = {
  { "SETTINGS", kTopItems, sizeof(kTopItems) / sizeof(kTopItems[0]), 0 },
  { "IDENTITY", nullptr, 0, PP_MENU_ACTION_IDENTITY },
};

static const PpMenu kMenu = { kPages, sizeof(kPages) / sizeof(kPages[0]), &kProvider, "A:select", "B:next" };

}  // namespace

const PpMenu& ppSettingsMenu()
{
  return kMenu;
}
