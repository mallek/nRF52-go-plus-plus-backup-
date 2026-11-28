#pragma once

#include <stdint.h>

// Table-driven menu: navigation and the view the screen draws. Pure: no Arduino,
// no hardware, no knowledge of what any setting is. Compiled into the sketch and
// also by the host tests in tests/menu/.
//
// A menu is a set of pages. Every page shows a Back row first (row 0), then its
// own rows. A page is either a fixed array of items or a list whose rows come
// from the provider (for example the identities in this build).
//
// The owner of a menu supplies the tables and a provider of plain functions.
// The logic reaches every value through that provider, so adding a setting is
// one table entry plus its provider case; nothing here changes.

static constexpr uint8_t PP_MENU_MAX_DEPTH = 3;
static constexpr uint8_t PP_MENU_MAX_VISIBLE = 8;
static constexpr uint32_t PP_MENU_TIMEOUT_MS = 20000;
static constexpr uint8_t PP_MENU_NONE = 0xff;

enum PpMenuItemKind : uint8_t {
  PP_MENU_CHOICE,   // SELECT steps to the next value
  PP_MENU_SUBMENU,  // SELECT opens another page
  PP_MENU_ACTION    // SELECT is reported to the caller
};

struct PpMenuItem {
  const char* label;
  PpMenuItemKind kind;
  uint8_t id;    // CHOICE: provider id. SUBMENU: provider id for the value text.
                 // ACTION: action id.
  uint8_t page;  // SUBMENU only: index of the page to open.
};

struct PpMenuPage {
  const char* title;
  const PpMenuItem* items;  // nullptr for a list page
  uint8_t itemCount;        // 0 for a list page
  uint8_t listId;           // list pages only: provider list id, also the action id
};

// Any pointer may be nullptr; the logic treats that as "no choices / no rows".
struct PpMenuProvider {
  uint8_t (*choiceCount)(uint8_t id);
  uint8_t (*choiceCurrent)(uint8_t id);
  const char* (*choiceText)(uint8_t id, uint8_t n);
  void (*choiceSet)(uint8_t id, uint8_t n);
  const char* (*valueText)(uint8_t id);  // value shown on a SUBMENU row
  uint8_t (*listCount)(uint8_t listId);
  const char* (*listText)(uint8_t listId, uint8_t n);
  uint8_t (*listActive)(uint8_t listId);  // PP_MENU_NONE if none
};

struct PpMenu {
  const PpMenuPage* pages;  // pages[0] is the top page
  uint8_t pageCount;
  const PpMenuProvider* provider;
  const char* hintLeft;
  const char* hintRight;
};

struct PpMenuState {
  bool open;
  uint8_t depth;  // 0 = top page
  uint8_t page[PP_MENU_MAX_DEPTH];
  uint8_t cursor[PP_MENU_MAX_DEPTH];  // row index; row 0 is Back
  uint32_t lastInputAt;
};

// Abstract inputs, so buttons and touch map to the same thing.
enum PpMenuInput : uint8_t { PP_MENU_IN_NEXT, PP_MENU_IN_SELECT, PP_MENU_IN_BACK };

enum PpMenuOutcome : uint8_t {
  PP_MENU_OUT_NONE,
  PP_MENU_OUT_REDRAW,
  PP_MENU_OUT_CLOSED,
  PP_MENU_OUT_ACTION  // id + arg say which; the menu stays open
};

struct PpMenuResult {
  PpMenuOutcome outcome;
  uint8_t id;
  uint8_t arg;
};

// Opens on the top page with the cursor on the first row after Back.
void ppMenuOpen(PpMenuState& st, const PpMenu& menu, uint32_t now);
PpMenuResult ppMenuInput(PpMenuState& st, const PpMenu& menu, PpMenuInput in, uint32_t now);
// A tap on absolute row `row` (0 = Back): moves the cursor there, then SELECT.
PpMenuResult ppMenuSelectRow(PpMenuState& st, const PpMenu& menu, uint8_t row, uint32_t now);
// Closes the menu after PP_MENU_TIMEOUT_MS without input.
PpMenuResult ppMenuTick(PpMenuState& st, uint32_t now);
// Rows on the current page, including the Back row. 0 when closed.
uint8_t ppMenuRowCount(const PpMenuState& st, const PpMenu& menu);
