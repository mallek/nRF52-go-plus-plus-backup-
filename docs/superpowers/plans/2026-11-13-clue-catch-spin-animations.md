# CLUE catch and spin animations Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 2 s on-screen animations on the CLUE for a catch (ball closes, stars rise) and a spin (coin-flip disk, sparks).

**Architecture:** A host-testable timing core (`pp_anim_logic`) decides which animation is playing and its progress 0..1000. A CLUE-only renderer (`pp_anim`) draws one frame per loop pass into a 160x160 GFXcanvas16 and pushes it to the panel center. `pp_display` starts animations from events and owns the backlight.

**Tech Stack:** Arduino C++ (Adafruit nRF52 1.7.0), Adafruit_GFX / ST7789, host g++ tests in WSL.

**Spec:** `docs/superpowers/specs/2026-11-13-clue-catch-spin-animations-design.md`

## Global Constraints

- Catch and spin only; about 2 s each (kAnimDurationMs = 2000). No sound. No bitmaps.
- Never block the loop: one frame per call, at most every 50 ms.
- Dark screen + catch/spin: light for the animation, then about 5 s (kAfterAnimHoldMs = 5000) on the status screen, then dark. No other event wakes the screen.
- No animation while the identity menu is open (count still updates).
- A newer event during an animation restarts with the newer animation.
- XIAO build unaffected: everything behind PP_HAS_DISPLAY.

## Review Focus

- Event during the menu: count changes, no animation, menu redraw intact.
- Two events back-to-back (catch then spin within 2 s): spin replaces catch.
- millis() wrap: progress math uses unsigned subtraction.
- Animation ends: status screen fully redrawn (frame + lines), not just lines.
- Button press during an animation: buttons keep working, A+B menu still opens and cancels the animation.

---

### Task 1: Timing core (host tested)

**Files:**
- Create: `code/nrf_emulator/pgpemu_xiao/pp_anim_logic.h`, `pp_anim_logic.cpp`
- Test: `tests/anim/anim_logic_test.cpp`

**Interfaces:**
- Produces: `enum PpAnimKind : uint8_t { PP_ANIM_NONE, PP_ANIM_CATCH, PP_ANIM_SPIN };`
  `struct PpAnimState { PpAnimKind kind; uint32_t startedAt; };`
  `void ppAnimStart(PpAnimState&, PpAnimKind, uint32_t now);`
  `uint16_t ppAnimProgress(const PpAnimState&, uint32_t now);` (0..1000, clamped)
  `bool ppAnimDone(const PpAnimState&, uint32_t now);` (true when kind NONE or elapsed >= kAnimDurationMs)
  `PpAnimKind ppAnimKindForEvent(PpFeedbackEvent);` (CAUGHT -> CATCH, POKESTOP_SPUN -> SPIN, else NONE)

- [ ] Test: start CATCH at 100 -> progress(100)=0, progress(1100)=500, progress(2100)=1000, done(2099)=false, done(2100)=true; start at 0xFFFFFF00 and check progress across the wrap; restart with SPIN mid-way resets startedAt and kind; kindForEvent maps the three cases.
- [ ] Run in WSL, see it fail to link; implement; run, PASS.
- [ ] Commit.

### Task 2: Renderer + display wiring

**Files:**
- Create: `code/nrf_emulator/pgpemu_xiao/pp_anim.h`, `pp_anim.cpp` (whole file inside `#if PP_HAS_DISPLAY`)
- Modify: `pp_display.cpp`

**Interfaces:**
- Consumes: Task 1.
- Produces: `void ppAnimDrawFrame(Adafruit_ST7789& tft, PpAnimKind kind, uint16_t progress);` draws into a static GFXcanvas16(160,160), blits to (40,40).

- [ ] Catch frames by progress: 0-250 ball halves fall in open; 250-450 halves close; 450-550 button flash; 550-1000 four stars rise and fade toward the background.
- [ ] Spin frames: disk width = |cos(theta)| * r with theta eased so it slows to 0 rad (face-on) by 800; 800-1000 six sparks burst radially.
- [ ] pp_display: in ppDisplayOnEvent, if mode is status and kind != NONE, start the animation and light the screen (backlight deadline >= now + 2000 + 5000). In ppDisplayUpdate, while animating, draw at most every 50 ms; when done, drawStatusFrame + drawStatusLines. Opening the menu cancels the animation. Drop the v2 10 s wake for other events.
- [ ] Compile for adafruit:nrf52:cluenrf52840 with dummy identities: no warnings from new files.
- [ ] Commit, then build the family UF2 and send it.
