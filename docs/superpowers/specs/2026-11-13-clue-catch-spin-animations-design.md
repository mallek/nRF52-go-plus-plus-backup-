# CLUE catch and spin animations (v3) - short design

Approved by Travis 2026-11-13 (in chat, from his phone at a tournament).

## Intent

The CLUE is Travis's daily-driver PGP+ clone. Catches and spins only bump a
counter today, and the screen is usually dark by then. He wants a quick, fun
2 second animation for the two good events, nothing more.

## Goals

- Catch: a ball drops in open (two halves), snaps shut, button flashes, then
  3-4 yellow stars pop out above it and drift up and fade. About 2 s.
- Spin: a round blue disk spins coin-style (horizontal width oscillates, so it
  narrows to a sliver and widens), slows, ends face-on, a few item sparks burst
  out. About 2 s.
- After the animation the normal status screen shows the updated counts.

## Non-goals

- No animation for flee, connect, disconnect, full or anything else.
- No sound.
- No bitmaps or flash-stored frames.

## Anti-goals

- Must never stall the BLE side or the GO handshake: no long blocking draws.
- Must not change XIAO behavior (no display there; everything is behind
  PP_HAS_DISPLAY).

## Approaches

1. Procedural shapes (circles, arcs, ellipses, lines) drawn into a small
   off-screen GFXcanvas16 and pushed per frame. Picked: a ball and a disk are
   geometry, tiny code, timing and colors easy to tweak, no flicker.
2. Pre-rendered bitmap frames in flash. Rejected: bigger, needs an asset
   pipeline, harder to tweak.
3. Drawing straight to the panel. Rejected: visible flicker on erase/redraw.

## Decided (Travis)

- Catch and spin only, about 2 s each, ball closing with stars, spinning disk.

## Decided (Claude's calls)

- If the screen is dark when a catch or spin arrives, it lights for the
  animation and stays on with the status screen about 5 s after, then dark.
  Other events do not wake the screen (supersedes the unshipped v2 10 s wake).
- One frame per loop pass, non-blocking, about 20 fps.
- Animations do not play while the identity menu is open; the count still
  updates.
- A new event during an animation restarts with the newer animation.

## Left to the builder

- Exact canvas size, frame timing, colors, star and spark paths.
