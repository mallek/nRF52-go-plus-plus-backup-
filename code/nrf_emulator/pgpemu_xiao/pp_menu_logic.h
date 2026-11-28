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
  // May be nullptr. When it returns non-null for an item's id, that text
  // replaces the item's static label.
  const char* (*labelText)(uint8_t id);
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
// Opens the menu with `page` on top of the top page, so Back returns to the top
// page. The top page's cursor is put on the SUBMENU row that leads to `page`
// when there is one. An invalid page, or page 0, behaves as ppMenuOpen.
void ppMenuOpenAt(PpMenuState& st, const PpMenu& menu, uint8_t page, uint32_t now);
// Restarts the idle timeout without an input (the caller was busy showing
// something else). Does nothing when the menu is closed.
void ppMenuKeepAlive(PpMenuState& st, uint32_t now);
PpMenuResult ppMenuInput(PpMenuState& st, const PpMenu& menu, PpMenuInput in, uint32_t now);
// A tap on absolute row `row` (0 = Back): moves the cursor there, then SELECT.
PpMenuResult ppMenuSelectRow(PpMenuState& st, const PpMenu& menu, uint8_t row, uint32_t now);
// Closes the menu after PP_MENU_TIMEOUT_MS without input.
PpMenuResult ppMenuTick(PpMenuState& st, uint32_t now);
// Rows on the current page, including the Back row. 0 when closed.
uint8_t ppMenuRowCount(const PpMenuState& st, const PpMenu& menu);

// Two-button boards: maps this loop's button state to an input. One button
// alone acts on its press edge. While both are down nothing acts, so the
// chord that opens the menu cannot turn into NEXT then SELECT inside it.
// Returns false when there is no input.
bool ppMenuTwoButtonInput(bool nextEdge, bool selectEdge, bool nextDown, bool selectDown, PpMenuInput& out);

// ---- view: what the screen draws -----------------------------------------

struct PpMenuRowView {
  const char* label;  // "Back" for the Back row; never nullptr
  const char* value;  // "" when the row has none; never nullptr. CHOICE, SUBMENU, ACTION rows
  bool isBack;
  bool selected;  // the cursor is here
  bool active;    // list row that is the current one
};

struct PpMenuView {
  const char* title;
  uint8_t firstRow;   // absolute index of rows[0], for mapping taps
  uint8_t rowCount;   // rows filled
  uint8_t totalRows;  // rows on the page
  PpMenuRowView rows[PP_MENU_MAX_VISIBLE];
  const char* hintLeft;
  const char* hintRight;
};

// Fills `out` with up to maxRows rows (clamped to 1..PP_MENU_MAX_VISIBLE),
// scrolled so the cursor row is always included. A closed menu gives 0 rows.
void ppMenuBuildView(const PpMenuState& st, const PpMenu& menu, uint8_t maxRows, PpMenuView& out);
