# Settings menu - design

Date: 2026-11-27
Status: draft for Travis's approval
Board for the first build: Adafruit CLUE. Must carry to the Seeed XIAO 1.47 in
touch display with no change to the menu logic.

## Intent

The device needs a real settings menu. Today the only on-screen list is the
identity switcher, hardcoded in `pp_display.cpp` and opened by holding A+B.
Every new option so far was going to be another button gesture, and the two
buttons are already crowded.

The menu is also the front door for PokeGachi, the pet game planned next: the
game is optional and is switched on or off here. The menu ships first and is
useful with no game at all.

Travis wants this built so features can be added and removed cleanly later.
That is the main design driver, ahead of speed.

## Goals

- One menu that holds every user setting, opened the way the identity menu is
  opened today (hold A+B 1.5 s).
- Adding a setting means adding one entry to a table, plus the code that reads
  the value. No change to menu navigation or drawing code.
- Menu logic has no screen or hardware code and is tested on the PC, the same
  way `pp_anim_logic` is.
- Settings are saved and survive a reboot.
- The same logic drives a 240x240 screen with two buttons now and a 172x320
  screen with two buttons plus touch later.

## Non-goals (this build)

- Catch mode, backlight level and control lock. They get menu entries later,
  one at a time, because each changes code outside the menu.
- Any PokeGachi game behaviour. The switch is stored and shown, nothing reads
  it yet.
- Touch input and the 1.47 display driver.
- A menu on boards with no display (plain XIAO). They load default settings.

## Anti-goals

- A second hardcoded list. If adding a setting needs edits in three places in
  drawing code, the design failed.
- Menu code that knows what an identity or an animation is.
- Menu work that delays BLE handling. Drawing and saving happen from `loop()`
  only, as the display code does today.

## What the user sees

```
  SETTINGS
   <-' Back
 > Identity     TRAVIS
   Animations   ON
   PokeGachi    OFF
 A:select        B:next
```

`Back` is the first row of every list and carries a return arrow (the
over-and-up arrow most menus use). The arrow is drawn as a small shape, not a
font character, so on-screen text stays 7-bit ASCII. When a list opens the
cursor starts on the first row after `Back`; in the identity list it starts
on the active identity. Pressing B on the last row wraps to `Back`, and once
more goes on to the first setting.

- Hold A+B 1.5 s on the status screen: the menu opens. Unlike today it opens
  even when the build has a single identity.
- B moves the cursor down and wraps to the top.
- A acts on the highlighted row:
  - on/off and multi-choice rows step to the next value, saved at once
  - a row with a sub-list opens it
  - `Back` returns to the parent list, or closes the menu from the top list
- 20 s with no press closes the menu from any depth.
- A list longer than the screen scrolls so the cursor stays visible.
- Identity is a sub-list of the identities in the build. The active one is
  marked. Choosing another shows the existing "SWITCHING TO" screen, then
  switches and reboots exactly as today. Choosing the active one returns to
  the settings list.
- Hold A 5 s for factory reset is unchanged and stays outside the menu. It
  does not fire while the menu is open (as today).

### Settings in the first build

| Setting | Values | Default | Effect |
|---|---|---|---|
| Identity | names in the build | saved selection | existing switch + reboot |
| Animations | ON, OFF | ON | OFF skips the catch, spin, ready and boot animations |
| PokeGachi | OFF, ON | OFF | stored only |

Animations OFF is a battery setting, not only a cosmetic one. With it OFF:

- The counters still update.
- A catch or spin on a lit screen does not extend the lit time.
- The GO handshake finishing (READY) does not light a dark screen.
- Boot shows the plain status screen for the normal lit time.
- Warnings (bag full and the like) still light the screen as today.
- The factory reset animation and the five-tap easter egg still play, because
  the user asked for those directly.

## Approaches considered

**A. Grow the current identity menu in place.** Extend `drawMenu` and
`updateMenu` in `pp_display.cpp`. Fastest. Rejected: it stays tied to the
240x240 panel and would be rewritten for the 1.47, and every setting would add
branches to the same two functions.

**B. A separate menu module split into logic, drawing and storage (chosen).**
Costs about a day more. The logic is tested on the PC and is reused unchanged
on the 1.47. Travis picked this for the clean extension path.

**C. Adopt the menu cards from Jack's firmware
(Jackv-makes/nRF52-PoGoPP-Emu).** Rejected: built for the tall screen only,
draws through its own canvas type, and does not run on the CLUE.

## Structure

Five units. Each has one job, and the arrows only point one way: display code
depends on the menu, the menu depends on nothing concrete.

```
pp_display.cpp (CLUE screen + buttons)
   |  feeds inputs, asks for a view, calls the drawer
   v
pp_menu_logic  <---- item table + value provider (plain functions)
   |                         ^
   v                         |
pp_menu_draw            pp_settings  ---->  pp_settings_logic
(Adafruit_GFX, w, h)    (InternalFS)        (pure: defaults, pack, unpack)
```

### 1. `pp_settings_logic.{h,cpp}` - pure

- `struct PpSettings` with one field per setting (`animations`, `pokegachi`
  for now).
- Defaults.
- Pack to and unpack from a byte buffer: a format version byte, the fields,
  and a checksum. Unpack rejects a wrong version or checksum and reports it,
  so the caller falls back to defaults. An older, shorter record loads with
  defaults for the fields it lacks, so adding a setting never wipes saved
  ones.
- No Arduino includes. Host tested.

### 2. `pp_settings.{h,cpp}` - storage

- Loads the record from InternalFS at boot (`/settings.bin`), saves on change
  only.
- `ppSettingsBegin()`, `const PpSettings& ppSettings()`, and one setter per
  field that saves when the value changed.
- Settings are per device, not per identity: an identity switch keeps them.
  A factory reset also keeps them, since the reset is about pairing.

### 3. `pp_menu_logic.{h,cpp}` - pure

The menu is data. A page is a title plus an array of items:

```
enum PpMenuItemKind : uint8_t { CHOICE, SUBMENU, ACTION, BACK };

struct PpMenuItem {
  const char* label;
  PpMenuItemKind kind;
  uint8_t id;        // meaning belongs to the owner of the table
};
```

The logic does not know what any `id` means. It reaches values through a small
provider the owner passes in: plain function pointers for "how many choices
does item `id` have", "which is current", "text of choice n", "set choice n",
and for sub-lists "how many rows" and "text of row n". The settings table and
the identity list are two providers. This is the seam that keeps identities,
animations and later PokeGachi out of the menu code.

State: a small stack of (page, cursor) so `Back` and nested lists work, plus
the time of the last press.

Inputs, as abstract actions so buttons and touch map to the same thing:
`NEXT`, `SELECT`, `BACK`, `SELECT_ROW(n)` (a tap on a row, unused on the
CLUE), and a time tick for the idle timeout.

Each input returns what the caller must do: nothing, redraw, close, or run an
action (`id`, argument). The identity switch is an action, so the reboot and
the "SWITCHING TO" screen stay in display code where they are today.

The logic also produces a view for the drawer: title, the rows that fit given
a row count, which row is selected, each row's value text, and the two hint
strings. Scrolling is decided here, so it is tested on the PC.

### 4. `pp_menu_draw.{h,cpp}` - drawing

One function: draw a view onto an `Adafruit_GFX` target of a given width and
height. Row height and text size come from the size passed in. No state, no
buttons. This is the only unit that changes look between the two screens, and
it should do so from the numbers, not from board `#if`s.

### 5. `pp_display.cpp` - changes

- `drawMenu` and `updateMenu` are removed. `openMenu`, `closeMenu` and the
  `MODE_MENU` handling stay but call the new units.
- Maps buttons to inputs: B = `NEXT`, A = `SELECT`.
- Owns the settings item table and the two providers, since it is the unit
  that already knows about identities and the screen.
- `ppDisplayOnEvent` and the boot path check `ppSettings().animations` before
  starting the catch, spin, ready and boot animations.
- The A+B combo no longer requires two identities.

## Tests

Host tests in `tests/menu/` and `tests/settings/`, same style as
`tests/anim/` (one `.cpp`, plain asserts, built with g++ in WSL):

- settings: defaults, pack then unpack round trip, bad checksum rejected,
  wrong version rejected, short older record fills new fields with defaults
- menu: cursor starts after Back, cursor wraps through Back, CHOICE steps and wraps and calls the setter, SUBMENU
  pushes and BACK pops, BACK on the top page closes, timeout closes from a
  sub-list, scrolling keeps the cursor in view, ACTION is returned with its
  argument, `SELECT_ROW` selects the tapped row, an empty sub-list is safe

On the device, Travis checks: menu opens with one identity and with three,
Animations OFF survives a reboot and silences the catch animation, identity
switch still works, hold A 5 s still resets and is blocked while the menu is
open.

## Constraints

- Braces on all control flow. No em dashes. On-screen text is 7-bit ASCII.
- No identity material in any committed file. Build and test with the dummy
  identity table.
- Compile checks never touch the real identity table. The builder works in a
  separate git worktree, generates the dummy table there
  (`tools/identity_table/gen_identities.py ... --dummy`), and compiles with
  `arduino-cli compile --fqbn adafruit:nrf52:cluenrf52840`. That worktree
  also keeps the untracked `code/tama_core` and `tests/tama` out of the
  branch. The one-identity and three-identity dummy builds for the device
  check come from there. Travis makes the real family build himself from his
  own checkout.
- No dynamic allocation in the menu units. Tables are `static const`.
- Flash writes only when a value changes.

## Left to the builder

- Exact names and signatures inside the units.
- Record layout and checksum choice.
- Row height, text sizes and how many rows fit on 240x240.
- Whether the item table lives in `pp_display.cpp` or a small
  `pp_menu_items.cpp` beside it, if `pp_display.cpp` gets too long.

## Calls made by Claude, not yet confirmed by Travis

- Settings survive a factory reset and an identity switch.
- Animations OFF leaves the reset animation and the easter egg playing.
- Animations OFF also stops catch, spin and READY from lighting or extending
  the screen (battery setting).
- Compile checks run in a separate worktree with the dummy identity table.
- The menu opens even with a single identity in the build.
- The cursor starts on the first row after `Back`, so opening the menu and
  pressing A never closes it by accident.

## Confirmed by Travis

- Approach B (separate logic, drawing and storage units).
- `Back` is a row with a return arrow, placed first in each list, and the
  cursor cycles past it. No long-press for back.
- Work goes on a new branch `feature/settings-menu` cut from
  `feature/clue-multi-id`, separate from the pet logic staged on
  `feature/pet-core`.
