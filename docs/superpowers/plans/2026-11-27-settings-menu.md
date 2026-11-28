# Settings Menu Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the hardcoded identity switcher on the CLUE with a table-driven settings menu (Identity, Animations, PokeGachi) whose logic is hardware-free, host tested, and reusable on the XIAO 1.47 in board.

**Architecture:** Pure logic units (`pp_settings_logic`, `pp_menu_logic`, one new function in `pp_anim_logic`) are tested on the PC. Thin hardware units sit on top: `pp_settings` (InternalFS), `pp_menu_draw` (Adafruit_GFX), `pp_menu_items` (the item table and value providers). `pp_display.cpp` keeps the buttons and screen and calls these.

**Tech Stack:** Arduino C++ on nRF52840 (`adafruit:nrf52:cluenrf52840`, arduino-cli 1.5.1), Adafruit_GFX / Adafruit_ST7789, Adafruit_LittleFS (InternalFS). Host tests: single-file g++ programs run in WSL.

**Spec:** `docs/superpowers/specs/2026-11-27-settings-menu-design.md`

Paths below are relative to the firmware repo root. `SK` means `code/nrf_emulator/pgpemu_xiao`.

## Global Constraints

- Braces on all control flow, even single-line bodies.
- No em dashes anywhere: code, comments, commit messages.
- On-screen text is 7-bit ASCII. The Back arrow is drawn as a shape, not a character.
- Never open, print, copy or commit `pp_identities.generated.h` from Travis's main checkout, or any `pp_device_material.h`.
- Work in a separate git worktree on branch `feature/settings-menu`, cut from `feature/clue-multi-id`. Do not bring the untracked `code/tama_core` or `tests/tama` into it.
- Compile checks use the dummy identity table generated inside the worktree, or no table at all.
- No dynamic allocation in the menu and settings units. Tables are `static const`.
- Flash is written only when a setting's value changes.
- Drawing and saving happen from `loop()` only, never from a BLE callback.
- Stage explicit paths, never `git add -A`. Commit locally. Do not push.
- End every commit message with `Co-Authored-By: Claude Fable 5.2 <noreply@anthropic.com>`.
- Menu idle timeout is 20000 ms. The A+B hold to open is 1500 ms (existing `kComboHoldMs`).

## Review Focus

1. A settings file that is empty, truncated, or has a length byte larger than the data: load defaults, no crash, no read past the buffer. (Task 1)
2. `millis()` wrapping during the 20 s idle timeout: the menu must still time out, and must not close at once. (Task 2)
3. A provider that reports zero choices, or a current value out of range: SELECT on that row must do nothing harmful (no divide by zero, no setter call with a bad index). (Task 2)
4. More identities than rows fit (the old menu silently capped at five): every identity stays reachable by scrolling. (Task 3)
5. A catch or spin arriving while the menu is open, and a button still held from the A+B combo when the menu opens: counters update, the menu is not drawn over, and the held A does not act as SELECT. (Task 6, device check)

## File Structure

| File | New | Responsibility |
|---|---|---|
| `SK/pp_settings_logic.{h,cpp}` | yes | `PpSettings`, defaults, pack and unpack. Pure. |
| `SK/pp_settings.{h,cpp}` | yes | Load and save the record on InternalFS. |
| `SK/pp_menu_logic.{h,cpp}` | yes | Menu data types, navigation, view building. Pure. |
| `SK/pp_menu_draw.{h,cpp}` | yes | Draw a `PpMenuView` on an `Adafruit_GFX`. |
| `SK/pp_menu_items.{h,cpp}` | yes | The settings menu table and its value providers. |
| `SK/pp_anim_logic.{h,cpp}` | no | Gains `ppEventScreenAction`. |
| `SK/pp_display.{h,cpp}` | no | Buttons, screen, uses the units above. Old `drawMenu`/`updateMenu` removed. |
| `SK/pgpemu_xiao.ino` | no | Calls `ppSettingsBegin()`. |
| `tests/settings/settings_logic_test.cpp` | yes | Host tests. |
| `tests/menu/menu_logic_test.cpp` | yes | Host tests. |
| `tests/anim/anim_logic_test.cpp` | no | Gains tests for `ppEventScreenAction`. |
| `README.md` | no | CLUE controls section updated. |

Host test convention (copy from `tests/anim/anim_logic_test.cpp`): one file, a `CHECK(cond)` macro that counts failures, `main` prints a summary and returns non-zero on any failure. Run from WSL, replacing `<wt>` with the worktree path under `/mnt/d/`:

```bash
wsl -e bash -c "cd <wt>/tests/<dir> && g++ -std=c++17 -Wall -Wextra -Werror -I../../code/nrf_emulator/pgpemu_xiao <test>.cpp <sources> -o t && ./t"
```

Compile gate (run in the worktree, Windows side):

```bash
arduino-cli compile --fqbn adafruit:nrf52:cluenrf52840 --output-dir build/clue code/nrf_emulator/pgpemu_xiao
```

---

### Task 1: Settings record (pure)

**Files:**
- Create: `SK/pp_settings_logic.h`, `SK/pp_settings_logic.cpp`
- Test: `tests/settings/settings_logic_test.cpp`

**Interfaces:**
- Produces:

```cpp
struct PpSettings {
  bool animations;   // default true
  bool pokegachi;    // default false
};
static constexpr uint8_t PP_SETTINGS_VERSION = 1;
static constexpr size_t PP_SETTINGS_MAX_RECORD = 32;

PpSettings ppSettingsDefaults();
// Returns bytes written, 0 if cap is too small.
size_t ppSettingsPack(const PpSettings& s, uint8_t* out, size_t cap);
// Always leaves `out` valid. Returns false (and out = defaults) on a bad record.
bool ppSettingsUnpack(const uint8_t* in, size_t len, PpSettings& out);
```

Record layout: `[0]` version, `[1]` payload length N, `[2 .. 2+N)` one byte per field in struct order (0 or 1), `[2+N]` checksum = (sum of all bytes before it) XOR 0xA5, as one byte.

Unpack rules: start from defaults. Reject (return false, defaults) when `len < 3`, version differs, `2 + N + 1 > len`, or checksum differs. A payload shorter than the current field count keeps defaults for the missing fields and returns true. Extra payload bytes are ignored. A field byte other than 0 or 1 keeps that field's default.

- [ ] **Step 1: Write the failing tests** in `tests/settings/settings_logic_test.cpp`:

```cpp
static void testDefaults() {
  PpSettings d = ppSettingsDefaults();
  CHECK(d.animations == true);
  CHECK(d.pokegachi == false);
}
static void testRoundTrip() {
  PpSettings s = { false, true };
  uint8_t buf[PP_SETTINGS_MAX_RECORD];
  size_t n = ppSettingsPack(s, buf, sizeof(buf));
  CHECK(n == 5);                      // version, len=2, 2 fields, checksum
  CHECK(buf[0] == PP_SETTINGS_VERSION);
  CHECK(buf[1] == 2);
  PpSettings out = ppSettingsDefaults();
  CHECK(ppSettingsUnpack(buf, n, out));
  CHECK(out.animations == false && out.pokegachi == true);
}
static void testPackTooSmall() {
  uint8_t buf[4];
  CHECK(ppSettingsPack(ppSettingsDefaults(), buf, sizeof(buf)) == 0);
}
static void testRejects() {
  PpSettings s = { false, true };
  uint8_t buf[PP_SETTINGS_MAX_RECORD];
  size_t n = ppSettingsPack(s, buf, sizeof(buf));
  PpSettings out = { false, true };
  CHECK(!ppSettingsUnpack(buf, 0, out));        // empty file
  CHECK(out.animations == true && out.pokegachi == false);
  CHECK(!ppSettingsUnpack(buf, n - 1, out));    // truncated
  buf[n - 1] ^= 0x01;
  CHECK(!ppSettingsUnpack(buf, n, out));        // bad checksum
  buf[n - 1] ^= 0x01;
  buf[0] = PP_SETTINGS_VERSION + 1;
  CHECK(!ppSettingsUnpack(buf, n, out));        // wrong version
  uint8_t lie[4] = { PP_SETTINGS_VERSION, 200, 0, 0 };
  CHECK(!ppSettingsUnpack(lie, sizeof(lie), out));  // length byte past the data
}
static void testOlderShortRecord() {
  // A record written when only `animations` existed.
  uint8_t rec[4] = { PP_SETTINGS_VERSION, 1, 0, 0 };
  rec[3] = static_cast<uint8_t>((rec[0] + rec[1] + rec[2]) ^ 0xA5);
  PpSettings out = { true, true };
  CHECK(ppSettingsUnpack(rec, sizeof(rec), out));
  CHECK(out.animations == false);
  CHECK(out.pokegachi == false);                // default for the missing field
}
static void testOutOfRangeFieldKeepsDefault() {
  uint8_t rec[5] = { PP_SETTINGS_VERSION, 2, 7, 1, 0 };
  rec[4] = static_cast<uint8_t>((rec[0] + rec[1] + rec[2] + rec[3]) ^ 0xA5);
  PpSettings out = ppSettingsDefaults();
  CHECK(ppSettingsUnpack(rec, sizeof(rec), out));
  CHECK(out.animations == true);                // 7 is not 0 or 1
  CHECK(out.pokegachi == true);
}
```

- [ ] **Step 2: Run to verify it fails.** Sources: `../../code/nrf_emulator/pgpemu_xiao/pp_settings_logic.cpp`. Expected: compile error, header not found.
- [ ] **Step 3: Implement** the three functions in `SK/pp_settings_logic.cpp`. Only `<stdint.h>` and `<stddef.h>`; no Arduino includes.
- [ ] **Step 4: Run the tests.** Expected: 0 failures.
- [ ] **Step 5: Commit** `SK/pp_settings_logic.h`, `SK/pp_settings_logic.cpp`, `tests/settings/settings_logic_test.cpp`. Message: `Settings: versioned record with pack and unpack, host tested`.

---

### Task 2: Menu navigation (pure)

**Files:**
- Create: `SK/pp_menu_logic.h`, `SK/pp_menu_logic.cpp`
- Test: `tests/menu/menu_logic_test.cpp`

**Interfaces:**
- Produces:

```cpp
static constexpr uint8_t PP_MENU_MAX_DEPTH = 3;
static constexpr uint32_t PP_MENU_TIMEOUT_MS = 20000;
static constexpr uint8_t PP_MENU_NONE = 0xff;

enum PpMenuItemKind : uint8_t { PP_MENU_CHOICE, PP_MENU_SUBMENU, PP_MENU_ACTION };

struct PpMenuItem {
  const char* label;
  PpMenuItemKind kind;
  uint8_t id;     // CHOICE: provider id. SUBMENU: provider id for the value text.
                  // ACTION: action id.
  uint8_t page;   // SUBMENU only: index of the page to open.
};

// A page is either a static item array, or a list whose rows come from the provider.
struct PpMenuPage {
  const char* title;
  const PpMenuItem* items;   // nullptr for a list page
  uint8_t itemCount;         // 0 for a list page
  uint8_t listId;            // list pages only: provider list id, also the action id
};

// Any pointer may be nullptr; the logic treats that as "no choices / no rows".
struct PpMenuProvider {
  uint8_t (*choiceCount)(uint8_t id);
  uint8_t (*choiceCurrent)(uint8_t id);
  const char* (*choiceText)(uint8_t id, uint8_t n);
  void (*choiceSet)(uint8_t id, uint8_t n);
  const char* (*valueText)(uint8_t id);          // value shown on a SUBMENU row
  uint8_t (*listCount)(uint8_t listId);
  const char* (*listText)(uint8_t listId, uint8_t n);
  uint8_t (*listActive)(uint8_t listId);         // PP_MENU_NONE if none
};

struct PpMenu {
  const PpMenuPage* pages;   // pages[0] is the top page
  uint8_t pageCount;
  const PpMenuProvider* provider;
  const char* hintLeft;      // "A:select" on the CLUE
  const char* hintRight;     // "B:next"
};

struct PpMenuState {
  bool open;
  uint8_t depth;                       // 0 = top page
  uint8_t page[PP_MENU_MAX_DEPTH];
  uint8_t cursor[PP_MENU_MAX_DEPTH];   // row index; row 0 is Back
  uint32_t lastInputAt;
};

enum PpMenuInput : uint8_t { PP_MENU_IN_NEXT, PP_MENU_IN_SELECT, PP_MENU_IN_BACK };
enum PpMenuOutcome : uint8_t { PP_MENU_OUT_NONE, PP_MENU_OUT_REDRAW, PP_MENU_OUT_CLOSED, PP_MENU_OUT_ACTION };
struct PpMenuResult { PpMenuOutcome outcome; uint8_t id; uint8_t arg; };

void ppMenuOpen(PpMenuState& st, const PpMenu& menu, uint32_t now);
PpMenuResult ppMenuInput(PpMenuState& st, const PpMenu& menu, PpMenuInput in, uint32_t now);
// A tap on absolute row `row` (0 = Back): moves the cursor there, then SELECT.
PpMenuResult ppMenuSelectRow(PpMenuState& st, const PpMenu& menu, uint8_t row, uint32_t now);
PpMenuResult ppMenuTick(PpMenuState& st, uint32_t now);
// Rows on the current page, including the Back row.
uint8_t ppMenuRowCount(const PpMenuState& st, const PpMenu& menu);
```

Rules:
- Every page has `1 + N` rows. Row 0 is Back.
- Opening a page puts the cursor on row 1. On a list page it goes on `1 + listActive` when that is in range. A page with no rows besides Back starts on row 0.
- NEXT moves down one row and wraps from the last row to row 0.
- SELECT on Back, or the BACK input: pop one level (REDRAW), or close from the top page (CLOSED, `open = false`).
- SELECT on a CHOICE row: if `choiceCount(id) > 0`, call `choiceSet(id, (current + 1) % count)` and return REDRAW. If the count is 0 or the provider pointers are null, return NONE. A current value at or past the count is treated as 0.
- SELECT on a SUBMENU row: push its page (REDRAW). If depth is already `PP_MENU_MAX_DEPTH - 1`, or the page index is out of range, return NONE.
- SELECT on an ACTION row: return ACTION with `id`, `arg = 0`. The menu stays open; the caller decides.
- SELECT on a list row n: if n equals `listActive`, pop one level (REDRAW). Otherwise return ACTION with `id = listId`, `arg = n`.
- Every input sets `lastInputAt = now`. `ppMenuTick` returns CLOSED and sets `open = false` when `(uint32_t)(now - lastInputAt) >= PP_MENU_TIMEOUT_MS`, from any depth; otherwise NONE.
- All calls on a closed menu return NONE.

- [ ] **Step 1: Write the failing tests.** Build a fixture in the test file: a top page with items `{"Identity", SUBMENU, id 0, page 1}`, `{"Animations", CHOICE, id 1}`, `{"Reset", ACTION, id 9}`; page 1 is a list page with `listId = 5`. The provider is backed by file-scope variables (`gChoice`, `gListCount`, `gListActive`, a `gSetCalls` counter). Tests, each with the listed assertions:

```cpp
static void testOpenStartsAfterBack();        // open, depth==0, cursor[0]==1
static void testNextWrapsThroughBack();       // rows=4: cursor 1,2,3,0,1 over four NEXTs
static void testChoiceStepsAndWraps();        // 2 choices: SELECT -> gChoice 1, REDRAW; again -> 0; gSetCalls==2
static void testChoiceWithZeroCountIsSafe();  // count 0 -> NONE, gSetCalls unchanged
static void testChoiceCurrentOutOfRange();    // current 9 of 2 -> set(1) (treated as 0, then +1)
static void testSubmenuPushAndBackRow();      // SELECT Identity -> depth 1; cursor on 1+gListActive;
                                              // move to row 0, SELECT -> depth 0, REDRAW, cursor[0] still 1
static void testBackInputPops();              // BACK at depth 1 -> depth 0; BACK at depth 0 -> CLOSED, !open
static void testBackRowOnTopCloses();         // cursor to row 0, SELECT -> CLOSED
static void testListSelectOther();            // gListActive 0, cursor on row 2 -> ACTION id 5 arg 1, still open
static void testListSelectActivePops();       // SELECT on active row -> REDRAW, depth 0
static void testEmptyListIsSafe();            // gListCount 0 -> rows==1, cursor 0, NEXT stays 0, SELECT pops
static void testActionRow();                  // SELECT Reset -> ACTION id 9 arg 0
static void testTimeoutFromSubmenu();         // open at 1000, enter list; tick 20999 -> NONE; tick 21000+ after
                                              // last input -> CLOSED, !open
static void testTimeoutAcrossWrap();          // open at 0xFFFFF000; tick at 0xFFFFF000+19999 -> NONE;
                                              // tick at 0xFFFFF000+20000 (wrapped) -> CLOSED
static void testSelectRow();                  // ppMenuSelectRow(row 2) -> cursor 2 and CHOICE stepped;
                                              // row >= rowCount -> NONE, cursor unchanged
static void testClosedMenuIgnoresInput();     // after CLOSED, NEXT/SELECT/tick -> NONE
static void testNullProviderPointers();       // provider with all nullptr: open, NEXT, SELECT each row -> no crash,
                                              // CHOICE -> NONE, list page rows == 1
static void testDepthLimit();                 // a page chain 3 deep: 3rd SUBMENU SELECT -> NONE, depth stays 2
```

- [ ] **Step 2: Run to verify it fails.** Sources: `../../code/nrf_emulator/pgpemu_xiao/pp_menu_logic.cpp`. Expected: header not found.
- [ ] **Step 3: Implement** the functions above in `SK/pp_menu_logic.cpp`. Only `<stdint.h>`; no Arduino includes.
- [ ] **Step 4: Run the tests.** Expected: 0 failures.
- [ ] **Step 5: Commit** the two source files and the test. Message: `Menu: table-driven navigation logic, host tested`.

---

### Task 3: Menu view and scrolling (pure)

**Files:**
- Modify: `SK/pp_menu_logic.h`, `SK/pp_menu_logic.cpp`
- Test: `tests/menu/menu_logic_test.cpp`

**Interfaces:**
- Consumes: everything from Task 2.
- Produces:

```cpp
static constexpr uint8_t PP_MENU_MAX_VISIBLE = 8;

struct PpMenuRowView {
  const char* label;   // "Back" for the Back row
  const char* value;   // "" when the row has none
  bool isBack;
  bool selected;       // the cursor is here
  bool active;         // list row that is the current one
};

struct PpMenuView {
  const char* title;
  uint8_t firstRow;    // absolute index of rows[0], for mapping taps
  uint8_t rowCount;    // rows filled, <= maxRows and <= PP_MENU_MAX_VISIBLE
  uint8_t totalRows;   // rows on the page
  PpMenuRowView rows[PP_MENU_MAX_VISIBLE];
  const char* hintLeft;
  const char* hintRight;
};

void ppMenuBuildView(const PpMenuState& st, const PpMenu& menu, uint8_t maxRows, PpMenuView& out);
```

Rules:
- `maxRows` is clamped to `1 .. PP_MENU_MAX_VISIBLE`.
- `firstRow` is 0 while the cursor is below `maxRows`, otherwise `cursor - maxRows + 1`. The cursor row is always in the view.
- Value text: CHOICE rows show `choiceText(id, current)`; SUBMENU rows show `valueText(id)`; ACTION, Back and list rows show `""`. A null pointer or null string from the provider becomes `""`. A null label becomes `""`.
- List rows take `label` from `listText` and set `active` where n equals `listActive`.
- On a closed menu the view has `rowCount = 0` and `title = ""`.

- [ ] **Step 1: Write the failing tests** using the Task 2 fixture:

```cpp
static void testViewTopPage();        // maxRows 5: title, rowCount 4, rows[0].isBack && label "Back",
                                      // rows[1] label "Identity" value == valueText, rows[2] value == choiceText,
                                      // rows[1].selected only, hints copied from the menu
static void testViewListMarksActive();// list of 3, active 1: rows[2].active only, rows[2].selected (cursor starts there)
static void testViewScrolls();        // list of 7 (8 rows), maxRows 4: cursor 1 -> firstRow 0, rowCount 4;
                                      // cursor 4 -> firstRow 1; cursor 7 -> firstRow 4, rows[3].selected;
                                      // wrap to cursor 0 -> firstRow 0, rows[0].isBack
static void testViewEveryRowReachable(); // list of 7, maxRows 4: step NEXT 8 times; at each step the view
                                         // contains exactly one selected row and its label matches the cursor
static void testViewMaxRowsClamped(); // maxRows 0 -> rowCount 1; maxRows 200 -> rowCount <= PP_MENU_MAX_VISIBLE
static void testViewNullStrings();    // provider texts return nullptr -> value "" (not nullptr), no crash
static void testViewClosed();         // closed menu -> rowCount 0
```

- [ ] **Step 2: Run to verify it fails.** Expected: `ppMenuBuildView` not declared.
- [ ] **Step 3: Implement** `ppMenuBuildView` in `SK/pp_menu_logic.cpp`.
- [ ] **Step 4: Run the tests.** Expected: 0 failures, Task 2 tests still pass.
- [ ] **Step 5: Commit.** Message: `Menu: view model with scrolling, host tested`.

---

### Task 4: What an event does to the screen (pure)

Today `ppDisplayOnEvent` decides this inline with `ppEventWakesScreen` and `ppAnimKindForEvent`. Move the decision into one tested function that also takes the Animations setting.

**Files:**
- Modify: `SK/pp_anim_logic.h`, `SK/pp_anim_logic.cpp`
- Test: `tests/anim/anim_logic_test.cpp`

**Interfaces:**
- Consumes: existing `ppEventWakesScreen(PpFeedbackEvent)`, `ppAnimKindForEvent(PpFeedbackEvent)`.
- Produces:

```cpp
enum PpScreenActionKind : uint8_t { PP_SCREEN_NOTHING, PP_SCREEN_ANIM, PP_SCREEN_LIGHT_WARNING };
struct PpScreenAction { PpScreenActionKind kind; PpAnimKind anim; };

PpScreenAction ppEventScreenAction(PpFeedbackEvent event, bool backlightOn, bool animationsOn);
```

Rules (animationsOn true must match today's behaviour exactly):

| animationsOn | Screen | Event | Result |
|---|---|---|---|
| true | dark | does not wake the screen | NOTHING |
| true | any other case | has an animation | ANIM with that kind |
| true | dark | wakes, no animation (warnings) | LIGHT_WARNING |
| true | lit | no animation | NOTHING |
| false | any | CAUGHT, POKESTOP_SPUN, READY | NOTHING |
| false | dark | POKEMON_FULL, BAG_FULL, STORAGE_FULL, BALLS_EMPTY | LIGHT_WARNING |
| false | lit | anything | NOTHING |

- [ ] **Step 1: Write the failing tests** in `tests/anim/anim_logic_test.cpp` and call them from `main`:

```cpp
static void testScreenActionAnimationsOn() {
  CHECK(ppEventScreenAction(PP_FEEDBACK_CAUGHT, false, true).kind == PP_SCREEN_NOTHING);
  PpScreenAction a = ppEventScreenAction(PP_FEEDBACK_CAUGHT, true, true);
  CHECK(a.kind == PP_SCREEN_ANIM && a.anim == PP_ANIM_CATCH);
  a = ppEventScreenAction(PP_FEEDBACK_POKESTOP_SPUN, true, true);
  CHECK(a.kind == PP_SCREEN_ANIM && a.anim == PP_ANIM_SPIN);
  a = ppEventScreenAction(PP_FEEDBACK_READY, false, true);
  CHECK(a.kind == PP_SCREEN_ANIM && a.anim == PP_ANIM_READY);
  CHECK(ppEventScreenAction(PP_FEEDBACK_BAG_FULL, false, true).kind == PP_SCREEN_LIGHT_WARNING);
  CHECK(ppEventScreenAction(PP_FEEDBACK_BAG_FULL, true, true).kind == PP_SCREEN_NOTHING);
  CHECK(ppEventScreenAction(PP_FEEDBACK_FLED, true, true).kind == PP_SCREEN_NOTHING);
}
static void testScreenActionAnimationsOff() {
  CHECK(ppEventScreenAction(PP_FEEDBACK_CAUGHT, true, false).kind == PP_SCREEN_NOTHING);
  CHECK(ppEventScreenAction(PP_FEEDBACK_POKESTOP_SPUN, true, false).kind == PP_SCREEN_NOTHING);
  CHECK(ppEventScreenAction(PP_FEEDBACK_READY, false, false).kind == PP_SCREEN_NOTHING);
  CHECK(ppEventScreenAction(PP_FEEDBACK_READY, true, false).kind == PP_SCREEN_NOTHING);
  CHECK(ppEventScreenAction(PP_FEEDBACK_BAG_FULL, false, false).kind == PP_SCREEN_LIGHT_WARNING);
  CHECK(ppEventScreenAction(PP_FEEDBACK_BALLS_EMPTY, false, false).kind == PP_SCREEN_LIGHT_WARNING);
  CHECK(ppEventScreenAction(PP_FEEDBACK_BAG_FULL, true, false).kind == PP_SCREEN_NOTHING);
}
```

- [ ] **Step 2: Run to verify it fails.** Build line is in the header comment of the test file. Expected: `ppEventScreenAction` not declared.
- [ ] **Step 3: Implement** `ppEventScreenAction` in `SK/pp_anim_logic.cpp`.
- [ ] **Step 4: Run the tests.** Expected: 0 failures, including every earlier test in the file.
- [ ] **Step 5: Commit.** Message: `Display logic: one tested decision for what an event does to the screen`.

---

### Task 5: Settings storage and boot wiring

**Files:**
- Create: `SK/pp_settings.h`, `SK/pp_settings.cpp`
- Modify: `SK/pgpemu_xiao.ino` (setup, between `ppIdentityBegin()` and `ppFeedbackBegin()`, near line 264)

**Interfaces:**
- Consumes: Task 1 (`PpSettings`, `ppSettingsDefaults`, `ppSettingsPack`, `ppSettingsUnpack`, `PP_SETTINGS_MAX_RECORD`).
- Produces:

```cpp
// Call after ppIdentityBegin(), which mounts InternalFS.
void ppSettingsBegin();
const PpSettings& ppSettings();
void ppSettingsSetAnimations(bool on);
void ppSettingsSetPokegachi(bool on);
```

Requirements:
- File path `/settings.bin` on `InternalFS`. Follow the read and write pattern of `readSelection` / `writeSelection` in `SK/pp_identity.cpp` (remove, then open for write).
- `ppSettingsBegin` reads up to `PP_SETTINGS_MAX_RECORD` bytes, unpacks, and prints one serial line: `Settings: loaded` or `Settings: defaults (no valid record)`. A missing file is not an error.
- Each setter returns without writing when the value is unchanged. On change it updates the in-memory copy first, then saves, and prints `Settings: could not save` if the open fails.
- `ppSettings()` before `ppSettingsBegin()` returns defaults.
- Compiles on every board, including the display-less XIAO. No `#if PP_HAS_DISPLAY` in this unit.
- Do not add `/settings.bin` to the files removed by the identity switch (`SK/pp_identity.cpp`) or the factory reset.

- [ ] **Step 1: Baseline compile gate** before any change in this task, to prove the worktree builds. With no generated identity table present the sketch uses its built-in fallback. Expected: `arduino-cli compile` succeeds. If it fails for a reason unrelated to this plan, stop and report.
- [ ] **Step 2: Implement** `SK/pp_settings.h` and `SK/pp_settings.cpp`, and add the `ppSettingsBegin()` call in `setup()`.
- [ ] **Step 3: Compile gate.** Expected: success, no new warnings from the new files.
- [ ] **Step 4: Commit** the two new files and the `.ino`. Message: `Settings: load at boot and save on change (InternalFS)`.

---

### Task 6: Menu on the CLUE screen

**Files:**
- Create: `SK/pp_menu_draw.h`, `SK/pp_menu_draw.cpp`, `SK/pp_menu_items.h`, `SK/pp_menu_items.cpp`
- Modify: `SK/pp_display.cpp`, `SK/pp_display.h`

**Interfaces:**
- Consumes: Tasks 2, 3, 5; `ppIdentityCount()`, `ppIdentityActive()`, `ppIdentityName(uint8_t)`, `ppIdentitySwitchTo(uint8_t)` from `SK/pp_identity.h`.
- Produces:

```cpp
// pp_menu_draw.h
struct PpMenuColors { uint16_t bg; uint16_t fg; uint16_t dim; uint16_t accent; };
// How many rows fit on a screen of height h (title and hint line excluded).
uint8_t ppMenuRowsThatFit(int16_t h);
void ppMenuDraw(Adafruit_GFX& gfx, int16_t w, int16_t h, const PpMenuView& view, const PpMenuColors& colors);

// pp_menu_items.h
static constexpr uint8_t PP_MENU_ACTION_IDENTITY = 1;   // arg = identity index
const PpMenu& ppSettingsMenu();
```

`pp_menu_items.cpp` requirements:
- Top page title `SETTINGS`, items in this order: `Identity` (SUBMENU, value = active identity name), `Animations` (CHOICE: `ON`, `OFF`), `PokeGachi` (CHOICE: `OFF`, `ON`). Choice order is the step order, so Animations steps ON to OFF and PokeGachi OFF to ON.
- Page 1: list page titled `IDENTITY`, `listId = PP_MENU_ACTION_IDENTITY`, rows from `ppIdentityCount` / `ppIdentityName` / `ppIdentityActive`.
- Providers read `ppSettings()` and call the Task 5 setters.
- Hints: `A:select` and `B:next`.
- This file has no drawing and no button code.

`pp_menu_draw.cpp` requirements:
- No state, no board `#if`. Geometry comes from `w` and `h`.
- Layout: title centred at the top in `dim`; rows below it; the two hints on the bottom line, left and right, in `dim`. The selected row is a filled `accent` bar with `bg` text, as the old `drawMenu` did. The value is right-aligned in the row. An `active` list row shows ` *` after its label.
- The Back row draws a return arrow (a short vertical stroke down into a horizontal stroke with an arrowhead pointing left) using `drawLine` / `fillTriangle`, followed by the text `Back`.
- Text that does not fit the row width is cut at the row edge, never wrapped.
- When `view.totalRows > view.rowCount`, draw a small marker at the right edge showing more rows exist above and/or below.
- Starting geometry for 240x240, to be tuned by eye: title at y 6 size 2, first row at y 34, row height 30, text size 2, hints at `h - 26`. `ppMenuRowsThatFit(240)` must return at least 5.

`pp_display.cpp` changes:
- Delete `drawMenu`, `updateMenu` and `menuIndex`, `menuActivityAt`. Add a file-scope `PpMenuState menuState`.
- `openMenu(now)`: stop any animation, `ppMenuOpen`, set `mode = MODE_MENU`, `waitForRelease = true`, light the screen, draw via `ppMenuBuildView` + `ppMenuDraw(tft, 240, 240, ...)` with the existing `kBg`, `kFg`, `kDim`, `kAccent`.
- In `MODE_MENU`: B pressed edge gives `PP_MENU_IN_NEXT`, A pressed edge gives `PP_MENU_IN_SELECT`; every loop also calls `ppMenuTick`. REDRAW redraws; CLOSED calls the existing `closeMenu()`; ACTION with `PP_MENU_ACTION_IDENTITY` shows the existing "SWITCHING TO" screen (the three `centerText` lines and `delay(800)` now in `updateMenu`) and calls `ppIdentitySwitchTo(arg)`.
- `updateCombo`: remove the `ppIdentityCount() < 2` early return so the menu opens with one identity.
- `redrawAfterWake`: in `MODE_MENU`, rebuild the view and draw it.
- Keep unchanged: `waitForRelease` handling, the wake-on-first-press rule, `ppUiOwnsButtons()` returning true while the menu is open, and the `mode != MODE_STATUS` early return in `ppDisplayOnEvent`.
- `pp_display.h`: rewrite the controls comment for the new menu (hold A+B 1.5 s opens settings; B next, A select; Back is the first row).

- [ ] **Step 1: Implement** `pp_menu_items` and `pp_menu_draw`.
- [ ] **Step 2: Rewire** `pp_display.cpp` and update `pp_display.h`.
- [ ] **Step 3: Compile gate with no identity table** (one built-in identity). Expected: success. Copy `build/clue` output aside as the one-identity build: run `python <adafruit core>/tools/uf2conv/uf2conv.py build/clue/pgpemu_xiao.ino.hex -c -f 0xADA52840 -o build/clue-menu-1id.uf2` (the core lives under the arduino-cli data dir, `packages/adafruit/hardware/nrf52/1.7.0`).
- [ ] **Step 4: Generate the dummy table and compile again.**

```bash
python tools/identity_table/gen_identities.py code/nrf_emulator/pgpemu_xiao/pp_identities.generated.h --dummy TEST1 TEST2 TEST3
```

Expected: compile success. Convert to `build/clue-menu-3id.uf2` the same way. Confirm `git status` does not list `pp_identities.generated.h` (it is gitignored).
- [ ] **Step 5: Run every host test** (`tests/settings`, `tests/menu`, `tests/anim`, `tests/feedback`, `tests/sleep`). Expected: 0 failures in each.
- [ ] **Step 6: Commit** the four new files and the two display files. Message: `CLUE: settings menu replaces the identity switcher`.

---

### Task 7: Animations setting takes effect, docs, device check

**Files:**
- Modify: `SK/pp_display.cpp` (`ppDisplayOnEvent` tail near line 590, `ppDisplayBegin` near line 424), `README.md` (CLUE controls section)

**Interfaces:**
- Consumes: Task 4 `ppEventScreenAction`, Task 5 `ppSettings()`.

Requirements:
- Replace the tail of `ppDisplayOnEvent` (from the `!backlightOn && !ppEventWakesScreen(event)` check to the end) with one call to `ppEventScreenAction(event, backlightOn, ppSettings().animations)`: ANIM calls `startAnim(kind, now)`, LIGHT_WARNING calls `touchBacklight(now, kWarningShowMs)`, NOTHING does nothing. The counters, `lastEvent` and `statusDirty` lines above it are untouched.
- `ppDisplayBegin`: start `PP_ANIM_BOOT` only when `ppSettings().animations` is true. The status screen and the normal lit time are already set just above.
- The easter egg and the reset animation are not gated.
- README: in the CLUE section, replace the identity menu description with the settings menu (how to open, B next, A select, Back row first, the three settings and what Animations OFF does). No em dashes.

- [ ] **Step 1: Make the two display changes.**
- [ ] **Step 2: Compile gate** with the dummy three-identity table. Expected: success. Refresh `build/clue-menu-3id.uf2`. Remove the generated table, compile, refresh `build/clue-menu-1id.uf2`.
- [ ] **Step 3: Run every host test.** Expected: 0 failures.
- [ ] **Step 4: Update `README.md`.**
- [ ] **Step 5: Commit** `SK/pp_display.cpp` and `README.md`. Message: `CLUE: Animations setting gates catch, spin, ready and boot`.
- [ ] **Step 6: Hand Travis the device checklist** and the two UF2 paths. Do not flash. The dummy builds cannot pair with GO, so the event checks need his real family build, which he makes from his own checkout once the branch is there.

Device checklist:

With `clue-menu-1id.uf2`:
1. Hold A+B 1.5 s: the menu opens. Cursor is on Identity, Back with the arrow is above it.
2. Keep A held from the combo: nothing is selected until release.
3. B walks Identity, Animations, PokeGachi, Back, Identity.
4. A on Animations flips it to OFF. Reboot: it is still OFF and no boot animation plays.
5. A on Identity opens the list with one name marked. A on it returns to settings.
6. Leave the menu alone 20 s: it closes.
7. With the menu open, hold A 5 s: no factory reset.
8. With the menu closed, hold A 5 s: the reset bar shows and the reset runs. After it, Animations is still OFF.

With `clue-menu-3id.uf2`:
9. Identity list shows TEST1, TEST2, TEST3, cursor on the active one.
10. Choose another: "SWITCHING TO" shows, the board reboots on the new identity, and the Animations setting is unchanged.

With Travis's real family build:
11. Animations ON, screen lit: a catch plays the animation, as before.
12. Animations OFF, screen lit: a catch updates the counter and plays nothing, and the screen goes dark on its normal timer.
13. Animations OFF, screen dark, reconnect to GO: the screen stays dark.
14. Open the menu and get a catch: the menu stays on screen; the counter is higher after closing it.

---

## Out of scope (from the spec)

Catch mode, backlight level, control lock, any PokeGachi behaviour, touch input, the 1.47 display driver.
