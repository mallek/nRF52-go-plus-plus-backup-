// Host tests for pp_menu_logic. Build + run from this folder (WSL or any g++):
//   g++ -std=c++17 -Wall -Wextra -Werror -I../../code/nrf_emulator/pgpemu_xiao
//       menu_logic_test.cpp ../../code/nrf_emulator/pgpemu_xiao/pp_menu_logic.cpp -o t
//   ./t
// Or all host tests: bash tests/run.sh

#include <cstdio>
#include <cstring>

#include "pp_menu_logic.h"

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

// ---- fixture -------------------------------------------------------------

static constexpr uint8_t kIdIdentity = 0;
static constexpr uint8_t kIdAnimations = 1;
static constexpr uint8_t kActReset = 9;
static constexpr uint8_t kListIdentity = 5;

static uint8_t gChoice = 0;
static uint8_t gChoiceCount = 2;
static int gSetCalls = 0;
static uint8_t gListCount = 3;
static uint8_t gListActive = 0;
static bool gNullTexts = false;

static uint8_t fxChoiceCount(uint8_t) { return gChoiceCount; }
static uint8_t fxChoiceCurrent(uint8_t) { return gChoice; }
static const char* fxChoiceText(uint8_t, uint8_t n)
{
  if (gNullTexts) {
    return nullptr;
  }
  return n == 0 ? "ON" : "OFF";
}
static void fxChoiceSet(uint8_t, uint8_t n)
{
  gChoice = n;
  ++gSetCalls;
}
static const char* fxValueText(uint8_t id)
{
  if (gNullTexts) {
    return nullptr;
  }
  if (id == kActReset) {
    return "3 berries";
  }
  return id == kIdIdentity ? "TRAVIS" : nullptr;
}
static bool gRetire = false;
static const char* fxLabelText(uint8_t id)
{
  if (gRetire && id == kActReset) {
    return "Retire";
  }
  return nullptr;
}
static uint8_t fxListCount(uint8_t) { return gListCount; }
static const char* fxListText(uint8_t, uint8_t n)
{
  static const char* names[] = { "N0", "N1", "N2", "N3", "N4", "N5", "N6" };
  if (gNullTexts || n >= 7) {
    return nullptr;
  }
  return names[n];
}
static uint8_t fxListActive(uint8_t) { return gListActive; }

static const PpMenuProvider kProvider = { fxChoiceCount, fxChoiceCurrent, fxChoiceText, fxChoiceSet,
                                          fxValueText,   fxListCount,     fxListText,   fxListActive,
                                          fxLabelText };
static const PpMenuProvider kNullProvider = { nullptr, nullptr, nullptr, nullptr, nullptr,
                                              nullptr, nullptr, nullptr, nullptr };
// Same as kProvider but without a label override.
static const PpMenuProvider kNoLabelProvider = { fxChoiceCount, fxChoiceCurrent, fxChoiceText, fxChoiceSet,
                                                 fxValueText,   fxListCount,     fxListText,   fxListActive,
                                                 nullptr };

static const PpMenuItem kTopItems[] = {
  { "Identity", PP_MENU_SUBMENU, kIdIdentity, 1 },
  { "Animations", PP_MENU_CHOICE, kIdAnimations, 0 },
  { "Reset", PP_MENU_ACTION, kActReset, 0 },
};
static const PpMenuPage kPages[] = {
  { "SETTINGS", kTopItems, 3, 0 },
  { "IDENTITY", nullptr, 0, kListIdentity },
};
static const PpMenu kMenu = { kPages, 2, &kProvider, "A:select", "B:next" };
static const PpMenu kNullMenu = { kPages, 2, &kNullProvider, "A:select", "B:next" };

// A chain three pages deep, each with one SUBMENU row to the next.
static const PpMenuItem kDeepA[] = { { "a", PP_MENU_SUBMENU, 0, 1 } };
static const PpMenuItem kDeepB[] = { { "b", PP_MENU_SUBMENU, 0, 2 } };
static const PpMenuItem kDeepC[] = { { "c", PP_MENU_SUBMENU, 0, 3 } };
static const PpMenuItem kDeepD[] = { { "d", PP_MENU_ACTION, 1, 0 } };
static const PpMenuPage kDeepPages[] = {
  { "A", kDeepA, 1, 0 },
  { "B", kDeepB, 1, 0 },
  { "C", kDeepC, 1, 0 },
  { "D", kDeepD, 1, 0 },
};
static const PpMenu kDeepMenu = { kDeepPages, 4, &kProvider, "", "" };

static void resetFixture()
{
  gChoice = 0;
  gChoiceCount = 2;
  gSetCalls = 0;
  gListCount = 3;
  gListActive = 0;
  gNullTexts = false;
  gRetire = false;
}

static PpMenuResult in(PpMenuState& st, PpMenuInput input, uint32_t now = 1000)
{
  return ppMenuInput(st, kMenu, input, now);
}

// ---- navigation ----------------------------------------------------------

static void testOpenStartsAfterBack()
{
  resetFixture();
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  CHECK(st.open);
  CHECK(st.depth == 0);
  CHECK(st.cursor[0] == 1);
  CHECK(ppMenuRowCount(st, kMenu) == 4);
}

static void testNextWrapsThroughBack()
{
  resetFixture();
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  CHECK(in(st, PP_MENU_IN_NEXT).outcome == PP_MENU_OUT_REDRAW);
  CHECK(st.cursor[0] == 2);
  in(st, PP_MENU_IN_NEXT);
  CHECK(st.cursor[0] == 3);
  in(st, PP_MENU_IN_NEXT);
  CHECK(st.cursor[0] == 0);
  in(st, PP_MENU_IN_NEXT);
  CHECK(st.cursor[0] == 1);
}

static void testChoiceStepsAndWraps()
{
  resetFixture();
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  in(st, PP_MENU_IN_NEXT);  // Animations
  CHECK(in(st, PP_MENU_IN_SELECT).outcome == PP_MENU_OUT_REDRAW);
  CHECK(gChoice == 1);
  CHECK(in(st, PP_MENU_IN_SELECT).outcome == PP_MENU_OUT_REDRAW);
  CHECK(gChoice == 0);
  CHECK(gSetCalls == 2);
  CHECK(st.open);
}

static void testChoiceWithZeroCountIsSafe()
{
  resetFixture();
  gChoiceCount = 0;
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  in(st, PP_MENU_IN_NEXT);
  CHECK(in(st, PP_MENU_IN_SELECT).outcome == PP_MENU_OUT_NONE);
  CHECK(gSetCalls == 0);
}

static void testChoiceCurrentOutOfRange()
{
  resetFixture();
  gChoice = 9;
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  in(st, PP_MENU_IN_NEXT);
  in(st, PP_MENU_IN_SELECT);
  CHECK(gChoice == 1);  // 9 of 2 is treated as 0, then stepped
  CHECK(gSetCalls == 1);
}

static void testSubmenuPushAndBackRow()
{
  resetFixture();
  gListActive = 1;
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  CHECK(in(st, PP_MENU_IN_SELECT).outcome == PP_MENU_OUT_REDRAW);  // Identity
  CHECK(st.depth == 1);
  CHECK(st.page[1] == 1);
  CHECK(st.cursor[1] == 2);  // 1 + active
  CHECK(ppMenuRowCount(st, kMenu) == 4);
  in(st, PP_MENU_IN_NEXT);  // row 3
  in(st, PP_MENU_IN_NEXT);  // row 0, Back
  CHECK(st.cursor[1] == 0);
  CHECK(in(st, PP_MENU_IN_SELECT).outcome == PP_MENU_OUT_REDRAW);
  CHECK(st.depth == 0);
  CHECK(st.cursor[0] == 1);
  CHECK(st.open);
}

static void testBackInputPops()
{
  resetFixture();
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  in(st, PP_MENU_IN_SELECT);
  CHECK(st.depth == 1);
  CHECK(in(st, PP_MENU_IN_BACK).outcome == PP_MENU_OUT_REDRAW);
  CHECK(st.depth == 0);
  CHECK(in(st, PP_MENU_IN_BACK).outcome == PP_MENU_OUT_CLOSED);
  CHECK(!st.open);
}

static void testBackRowOnTopCloses()
{
  resetFixture();
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  in(st, PP_MENU_IN_NEXT);
  in(st, PP_MENU_IN_NEXT);
  in(st, PP_MENU_IN_NEXT);
  CHECK(st.cursor[0] == 0);
  CHECK(in(st, PP_MENU_IN_SELECT).outcome == PP_MENU_OUT_CLOSED);
  CHECK(!st.open);
}

static void testListSelectOther()
{
  resetFixture();
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  in(st, PP_MENU_IN_SELECT);
  CHECK(st.cursor[1] == 1);
  in(st, PP_MENU_IN_NEXT);
  PpMenuResult r = in(st, PP_MENU_IN_SELECT);
  CHECK(r.outcome == PP_MENU_OUT_ACTION);
  CHECK(r.id == kListIdentity);
  CHECK(r.arg == 1);
  CHECK(st.open);
}

static void testListSelectActivePops()
{
  resetFixture();
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  in(st, PP_MENU_IN_SELECT);
  CHECK(in(st, PP_MENU_IN_SELECT).outcome == PP_MENU_OUT_REDRAW);
  CHECK(st.depth == 0);
}

static void testEmptyListIsSafe()
{
  resetFixture();
  gListCount = 0;
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  in(st, PP_MENU_IN_SELECT);
  CHECK(st.depth == 1);
  CHECK(ppMenuRowCount(st, kMenu) == 1);
  CHECK(st.cursor[1] == 0);
  in(st, PP_MENU_IN_NEXT);
  CHECK(st.cursor[1] == 0);
  CHECK(in(st, PP_MENU_IN_SELECT).outcome == PP_MENU_OUT_REDRAW);
  CHECK(st.depth == 0);
}

static void testListActiveOutOfRange()
{
  resetFixture();
  gListActive = 200;
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  in(st, PP_MENU_IN_SELECT);
  CHECK(st.cursor[1] == 1);
}

static void testActionRow()
{
  resetFixture();
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  in(st, PP_MENU_IN_NEXT);
  in(st, PP_MENU_IN_NEXT);
  PpMenuResult r = in(st, PP_MENU_IN_SELECT);
  CHECK(r.outcome == PP_MENU_OUT_ACTION);
  CHECK(r.id == kActReset);
  CHECK(r.arg == 0);
  CHECK(st.open);
}

static void testTimeoutFromSubmenu()
{
  resetFixture();
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  in(st, PP_MENU_IN_SELECT, 2000);
  CHECK(st.depth == 1);
  CHECK(ppMenuTick(st, 21999).outcome == PP_MENU_OUT_NONE);
  CHECK(st.open);
  CHECK(ppMenuTick(st, 22000).outcome == PP_MENU_OUT_CLOSED);
  CHECK(!st.open);
}

static void testTimeoutAcrossWrap()
{
  resetFixture();
  PpMenuState st = {};
  uint32_t t0 = 0xFFFFF000u;
  ppMenuOpen(st, kMenu, t0);
  CHECK(ppMenuTick(st, t0 + 1).outcome == PP_MENU_OUT_NONE);
  CHECK(ppMenuTick(st, t0 + 19999u).outcome == PP_MENU_OUT_NONE);  // already wrapped past 0
  CHECK(st.open);
  CHECK(ppMenuTick(st, t0 + 20000u).outcome == PP_MENU_OUT_CLOSED);
  CHECK(!st.open);
}

static void testSelectRow()
{
  resetFixture();
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  CHECK(ppMenuSelectRow(st, kMenu, 2, 1000).outcome == PP_MENU_OUT_REDRAW);
  CHECK(st.cursor[0] == 2);
  CHECK(gChoice == 1);
  CHECK(ppMenuSelectRow(st, kMenu, 4, 1000).outcome == PP_MENU_OUT_NONE);
  CHECK(st.cursor[0] == 2);
  CHECK(ppMenuSelectRow(st, kMenu, 0, 1000).outcome == PP_MENU_OUT_CLOSED);
}

static void testClosedMenuIgnoresInput()
{
  resetFixture();
  PpMenuState st = {};
  CHECK(in(st, PP_MENU_IN_NEXT).outcome == PP_MENU_OUT_NONE);
  CHECK(in(st, PP_MENU_IN_SELECT).outcome == PP_MENU_OUT_NONE);
  CHECK(ppMenuTick(st, 99999).outcome == PP_MENU_OUT_NONE);
  CHECK(ppMenuSelectRow(st, kMenu, 1, 1000).outcome == PP_MENU_OUT_NONE);
  CHECK(ppMenuRowCount(st, kMenu) == 0);
  CHECK(gSetCalls == 0);
}

static void testNullProviderPointers()
{
  resetFixture();
  PpMenuState st = {};
  ppMenuOpen(st, kNullMenu, 1000);
  CHECK(ppMenuInput(st, kNullMenu, PP_MENU_IN_SELECT, 1000).outcome == PP_MENU_OUT_REDRAW);  // into the list
  CHECK(ppMenuRowCount(st, kNullMenu) == 1);
  CHECK(ppMenuInput(st, kNullMenu, PP_MENU_IN_SELECT, 1000).outcome == PP_MENU_OUT_REDRAW);  // Back
  ppMenuInput(st, kNullMenu, PP_MENU_IN_NEXT, 1000);
  CHECK(ppMenuInput(st, kNullMenu, PP_MENU_IN_SELECT, 1000).outcome == PP_MENU_OUT_NONE);  // CHOICE
  ppMenuInput(st, kNullMenu, PP_MENU_IN_NEXT, 1000);
  CHECK(ppMenuInput(st, kNullMenu, PP_MENU_IN_SELECT, 1000).outcome == PP_MENU_OUT_ACTION);

  // No provider object at all.
  PpMenu bare = { kPages, 2, nullptr, nullptr, nullptr };
  PpMenuState st2 = {};
  ppMenuOpen(st2, bare, 1000);
  ppMenuInput(st2, bare, PP_MENU_IN_NEXT, 1000);
  CHECK(ppMenuInput(st2, bare, PP_MENU_IN_SELECT, 1000).outcome == PP_MENU_OUT_NONE);

  // No pages: the menu does not open.
  PpMenu empty = { nullptr, 0, &kProvider, "", "" };
  PpMenuState st3 = {};
  ppMenuOpen(st3, empty, 1000);
  CHECK(!st3.open);
}

static void testDepthLimit()
{
  resetFixture();
  PpMenuState st = {};
  ppMenuOpen(st, kDeepMenu, 1000);
  CHECK(ppMenuInput(st, kDeepMenu, PP_MENU_IN_SELECT, 1000).outcome == PP_MENU_OUT_REDRAW);
  CHECK(ppMenuInput(st, kDeepMenu, PP_MENU_IN_SELECT, 1000).outcome == PP_MENU_OUT_REDRAW);
  CHECK(st.depth == 2);
  CHECK(ppMenuInput(st, kDeepMenu, PP_MENU_IN_SELECT, 1000).outcome == PP_MENU_OUT_NONE);
  CHECK(st.depth == 2);
}

static void testSubmenuPageOutOfRange()
{
  static const PpMenuItem items[] = { { "bad", PP_MENU_SUBMENU, 0, 7 } };
  static const PpMenuPage pages[] = { { "T", items, 1, 0 } };
  PpMenu menu = { pages, 1, &kProvider, "", "" };
  PpMenuState st = {};
  ppMenuOpen(st, menu, 1000);
  CHECK(ppMenuInput(st, menu, PP_MENU_IN_SELECT, 1000).outcome == PP_MENU_OUT_NONE);
  CHECK(st.depth == 0);
}

// ---- view ----------------------------------------------------------------

static int selectedCount(const PpMenuView& v)
{
  int n = 0;
  for (uint8_t i = 0; i < v.rowCount; ++i) {
    if (v.rows[i].selected) {
      ++n;
    }
  }
  return n;
}

static void testViewTopPage()
{
  resetFixture();
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  PpMenuView v;
  ppMenuBuildView(st, kMenu, 5, v);
  CHECK(std::strcmp(v.title, "SETTINGS") == 0);
  CHECK(v.firstRow == 0);
  CHECK(v.rowCount == 4);
  CHECK(v.totalRows == 4);
  CHECK(v.rows[0].isBack);
  CHECK(std::strcmp(v.rows[0].label, "Back") == 0);
  CHECK(std::strcmp(v.rows[0].value, "") == 0);
  CHECK(!v.rows[1].isBack);
  CHECK(std::strcmp(v.rows[1].label, "Identity") == 0);
  CHECK(std::strcmp(v.rows[1].value, "TRAVIS") == 0);
  CHECK(std::strcmp(v.rows[2].label, "Animations") == 0);
  CHECK(std::strcmp(v.rows[2].value, "ON") == 0);
  CHECK(std::strcmp(v.rows[3].value, "3 berries") == 0);  // ACTION rows show valueText too
  CHECK(v.rows[1].selected);
  CHECK(selectedCount(v) == 1);
  CHECK(std::strcmp(v.hintLeft, "A:select") == 0);
  CHECK(std::strcmp(v.hintRight, "B:next") == 0);
  gChoice = 1;
  ppMenuBuildView(st, kMenu, 5, v);
  CHECK(std::strcmp(v.rows[2].value, "OFF") == 0);
}

static void testViewListMarksActive()
{
  resetFixture();
  gListActive = 1;
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  in(st, PP_MENU_IN_SELECT);
  PpMenuView v;
  ppMenuBuildView(st, kMenu, 5, v);
  CHECK(std::strcmp(v.title, "IDENTITY") == 0);
  CHECK(v.rowCount == 4);
  CHECK(v.rows[0].isBack);
  CHECK(std::strcmp(v.rows[1].label, "N0") == 0);
  CHECK(!v.rows[1].active);
  CHECK(v.rows[2].active);
  CHECK(v.rows[2].selected);
  CHECK(!v.rows[3].active);
  CHECK(selectedCount(v) == 1);
}

static void testViewScrolls()
{
  resetFixture();
  gListCount = 7;  // 8 rows with Back
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  in(st, PP_MENU_IN_SELECT);
  PpMenuView v;
  ppMenuBuildView(st, kMenu, 4, v);
  CHECK(v.totalRows == 8);
  CHECK(v.firstRow == 0);
  CHECK(v.rowCount == 4);
  in(st, PP_MENU_IN_NEXT);
  in(st, PP_MENU_IN_NEXT);
  in(st, PP_MENU_IN_NEXT);  // cursor 4
  ppMenuBuildView(st, kMenu, 4, v);
  CHECK(v.firstRow == 1);
  CHECK(v.rows[3].selected);
  in(st, PP_MENU_IN_NEXT);
  in(st, PP_MENU_IN_NEXT);
  in(st, PP_MENU_IN_NEXT);  // cursor 7, last row
  ppMenuBuildView(st, kMenu, 4, v);
  CHECK(v.firstRow == 4);
  CHECK(v.rowCount == 4);
  CHECK(v.rows[3].selected);
  CHECK(std::strcmp(v.rows[3].label, "N6") == 0);
  in(st, PP_MENU_IN_NEXT);  // wraps to Back
  ppMenuBuildView(st, kMenu, 4, v);
  CHECK(v.firstRow == 0);
  CHECK(v.rows[0].isBack);
  CHECK(v.rows[0].selected);
}

static void testViewEveryRowReachable()
{
  resetFixture();
  gListCount = 7;
  gListActive = PP_MENU_NONE;
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  in(st, PP_MENU_IN_SELECT);
  bool seen[8] = { false, false, false, false, false, false, false, false };
  for (int step = 0; step < 8; ++step) {
    PpMenuView v;
    ppMenuBuildView(st, kMenu, 4, v);
    CHECK(selectedCount(v) == 1);
    uint8_t cursor = st.cursor[st.depth];
    CHECK(cursor >= v.firstRow && cursor < v.firstRow + v.rowCount);
    const PpMenuRowView& row = v.rows[cursor - v.firstRow];
    CHECK(row.selected);
    if (cursor == 0) {
      CHECK(row.isBack);
    } else {
      CHECK(std::strcmp(row.label, fxListText(0, static_cast<uint8_t>(cursor - 1))) == 0);
    }
    seen[cursor] = true;
    in(st, PP_MENU_IN_NEXT);
  }
  for (int i = 0; i < 8; ++i) {
    CHECK(seen[i]);
  }
}

static void testViewMaxRowsClamped()
{
  resetFixture();
  gListCount = 7;
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  in(st, PP_MENU_IN_SELECT);
  PpMenuView v;
  ppMenuBuildView(st, kMenu, 0, v);
  CHECK(v.rowCount == 1);
  CHECK(v.rows[0].selected);
  ppMenuBuildView(st, kMenu, 200, v);
  CHECK(v.rowCount <= PP_MENU_MAX_VISIBLE);
  CHECK(v.rowCount == 8);
}

static void testViewNullStrings()
{
  resetFixture();
  gNullTexts = true;
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  PpMenuView v;
  ppMenuBuildView(st, kMenu, 5, v);
  CHECK(v.rows[1].value != nullptr && std::strcmp(v.rows[1].value, "") == 0);
  CHECK(v.rows[2].value != nullptr && std::strcmp(v.rows[2].value, "") == 0);
  in(st, PP_MENU_IN_SELECT);
  ppMenuBuildView(st, kMenu, 5, v);
  CHECK(v.rows[1].label != nullptr && std::strcmp(v.rows[1].label, "") == 0);

  PpMenu bare = { kPages, 2, nullptr, nullptr, nullptr };
  PpMenuState st2 = {};
  ppMenuOpen(st2, bare, 1000);
  ppMenuBuildView(st2, bare, 5, v);
  CHECK(v.hintLeft != nullptr && v.hintRight != nullptr);
  CHECK(std::strcmp(v.rows[2].value, "") == 0);
}

static void testViewClosed()
{
  resetFixture();
  PpMenuState st = {};
  PpMenuView v;
  ppMenuBuildView(st, kMenu, 5, v);
  CHECK(v.rowCount == 0);
  CHECK(v.totalRows == 0);
  CHECK(std::strcmp(v.title, "") == 0);
}

static void testTwoButtonInput()
{
  PpMenuInput out = PP_MENU_IN_BACK;
  // nothing pressed
  CHECK(!ppMenuTwoButtonInput(false, false, false, false, out));
  // B alone = next, A alone = select
  CHECK(ppMenuTwoButtonInput(true, false, true, false, out) && out == PP_MENU_IN_NEXT);
  CHECK(ppMenuTwoButtonInput(false, true, false, true, out) && out == PP_MENU_IN_SELECT);
  // The A+B chord that opened the menu, pressed again inside it: B lands
  // first, then A one loop later while B is still down. Neither may act, or
  // the row after the cursor gets selected.
  CHECK(!ppMenuTwoButtonInput(false, true, true, true, out));
  CHECK(!ppMenuTwoButtonInput(true, false, true, true, out));
  CHECK(!ppMenuTwoButtonInput(true, true, true, true, out));
}

// ---- additions: open at a page, action values, label override, keep-alive ---

static void testOpenAtPage()
{
  resetFixture();
  gListActive = 1;
  PpMenuState st = {};
  ppMenuOpenAt(st, kMenu, 1, 1000);
  CHECK(st.open);
  CHECK(st.depth == 1);
  CHECK(st.page[0] == 0);
  CHECK(st.page[1] == 1);
  CHECK(st.cursor[0] == 1);  // the Identity row
  CHECK(st.cursor[1] == 2);  // as a normal push places it: 1 + active
  CHECK(st.lastInputAt == 1000);
  CHECK(ppMenuRowCount(st, kMenu) == 4);
  PpMenuResult r = in(st, PP_MENU_IN_BACK);
  CHECK(r.outcome == PP_MENU_OUT_REDRAW);
  CHECK(st.depth == 0);
  CHECK(st.open);
  CHECK(st.cursor[0] == 1);
}

static void testOpenAtInvalid()
{
  resetFixture();
  PpMenuState ref = {};
  ppMenuOpen(ref, kMenu, 1000);
  PpMenuState a = {};
  ppMenuOpenAt(a, kMenu, 9, 1000);
  PpMenuState b = {};
  ppMenuOpenAt(b, kMenu, 0, 1000);
  const PpMenuState* both[2] = { &a, &b };
  for (int i = 0; i < 2; ++i) {
    CHECK(both[i]->open == ref.open);
    CHECK(both[i]->depth == ref.depth);
    CHECK(both[i]->lastInputAt == ref.lastInputAt);
    for (uint8_t d = 0; d < PP_MENU_MAX_DEPTH; ++d) {
      CHECK(both[i]->page[d] == ref.page[d]);
      CHECK(both[i]->cursor[d] == ref.cursor[d]);
    }
  }
  // No pages: does not open.
  PpMenu empty = { nullptr, 0, &kProvider, "", "" };
  PpMenuState c = {};
  ppMenuOpenAt(c, empty, 1, 1000);
  CHECK(!c.open);
}

static void testOpenAtPageWithoutSubmenuRow()
{
  // Page 1 is not reachable from the top page: Back still returns to the top
  // page, whose cursor stays where a normal open puts it.
  static const PpMenuItem items[] = { { "x", PP_MENU_ACTION, 1, 0 } };
  static const PpMenuPage pages[] = { { "T", items, 1, 0 }, { "U", nullptr, 0, kListIdentity } };
  PpMenu menu = { pages, 2, &kProvider, "", "" };
  PpMenuState st = {};
  ppMenuOpenAt(st, menu, 1, 1000);
  CHECK(st.open);
  CHECK(st.depth == 1);
  CHECK(st.cursor[0] == 1);
  CHECK(ppMenuInput(st, menu, PP_MENU_IN_BACK, 1000).outcome == PP_MENU_OUT_REDRAW);
  CHECK(st.depth == 0);
}

static void testActionRowShowsValue()
{
  resetFixture();
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  PpMenuView v;
  ppMenuBuildView(st, kMenu, 5, v);
  CHECK(std::strcmp(v.rows[3].label, "Reset") == 0);
  CHECK(std::strcmp(v.rows[3].value, "3 berries") == 0);
  // Other kinds are unchanged.
  CHECK(std::strcmp(v.rows[1].value, "TRAVIS") == 0);
  CHECK(std::strcmp(v.rows[2].value, "ON") == 0);
}

static void testLabelOverride()
{
  resetFixture();
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  PpMenuView v;
  ppMenuBuildView(st, kMenu, 5, v);
  CHECK(std::strcmp(v.rows[3].label, "Reset") == 0);
  gRetire = true;
  ppMenuBuildView(st, kMenu, 5, v);
  CHECK(std::strcmp(v.rows[3].label, "Retire") == 0);
  CHECK(std::strcmp(v.rows[1].label, "Identity") == 0);  // nullptr for other ids keeps the static label
}

static void testLabelOverrideNullProviderMember()
{
  resetFixture();
  gRetire = true;
  PpMenu menu = { kPages, 2, &kNoLabelProvider, "", "" };
  PpMenuState st = {};
  ppMenuOpen(st, menu, 1000);
  PpMenuView v;
  ppMenuBuildView(st, menu, 5, v);
  CHECK(std::strcmp(v.rows[3].label, "Reset") == 0);
  PpMenuState st2 = {};
  ppMenuOpen(st2, kNullMenu, 1000);
  ppMenuBuildView(st2, kNullMenu, 5, v);
  CHECK(std::strcmp(v.rows[3].label, "Reset") == 0);
}

static void testKeepAlive()
{
  resetFixture();
  PpMenuState st = {};
  ppMenuOpen(st, kMenu, 1000);
  ppMenuKeepAlive(st, 15000);
  CHECK(ppMenuTick(st, 34999).outcome == PP_MENU_OUT_NONE);
  CHECK(st.open);
  CHECK(ppMenuTick(st, 35000).outcome == PP_MENU_OUT_CLOSED);
  CHECK(!st.open);

  PpMenuState closed = {};
  closed.lastInputAt = 77;
  ppMenuKeepAlive(closed, 15000);
  CHECK(!closed.open);
  CHECK(closed.lastInputAt == 77);
  CHECK(closed.depth == 0);
}

int main()
{
  testOpenStartsAfterBack();
  testNextWrapsThroughBack();
  testChoiceStepsAndWraps();
  testChoiceWithZeroCountIsSafe();
  testChoiceCurrentOutOfRange();
  testSubmenuPushAndBackRow();
  testBackInputPops();
  testBackRowOnTopCloses();
  testListSelectOther();
  testListSelectActivePops();
  testEmptyListIsSafe();
  testListActiveOutOfRange();
  testActionRow();
  testTimeoutFromSubmenu();
  testTimeoutAcrossWrap();
  testSelectRow();
  testClosedMenuIgnoresInput();
  testNullProviderPointers();
  testDepthLimit();
  testSubmenuPageOutOfRange();
  testViewTopPage();
  testViewListMarksActive();
  testViewScrolls();
  testViewEveryRowReachable();
  testViewMaxRowsClamped();
  testViewNullStrings();
  testViewClosed();
  testTwoButtonInput();
  testOpenAtPage();
  testOpenAtInvalid();
  testOpenAtPageWithoutSubmenuRow();
  testActionRowShowsValue();
  testLabelOverride();
  testLabelOverrideNullProviderMember();
  testKeepAlive();
  if (failures == 0) {
    std::printf("menu_logic: all passed\n");
    return 0;
  }
  std::printf("menu_logic: %d failure(s)\n", failures);
  return 1;
}
