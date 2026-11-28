#pragma once

#include <stdint.h>

#include "pp_gachi_stage.h"

// PokeGachi sprite table. The pixel data comes from a generated header made by
// tools/sprites/gen_sprites.py (gitignored). Without it the table is empty and
// every lookup returns nullptr, so the UI draws a placeholder.
struct PpGachiAnim {
  uint8_t w, h;          // one frame, cropped
  uint8_t frames;        // 0 = this species has no such animation
  int8_t originX;        // columns from the crop's left edge to the frame's horizontal centre
  uint8_t baseline;      // rows from the crop's top to the ground line (the lowest drawn row across all frames, plus one)
  const uint16_t* ms;    // `frames` entries
  const uint8_t* pixels; // frames one after another; each frame h rows of 4 bits per pixel, high nibble first, rows padded to a byte
};

struct PpGachiSprite {
  uint8_t dex;
  bool shiny;
  const uint16_t* palette;            // 16 RGB565 entries; entry 0 is transparent
  PpGachiAnim anim[PP_SANIM_COUNT];
};

// The sprite for a species; shiny falls back to the normal sprite when the shiny is missing.
// nullptr when the build has no sprite for it (the UI then draws a placeholder).
const PpGachiSprite* ppGachiSpriteFor(uint8_t dex, bool shiny);

// The animation to draw: the one asked for, or Idle when it is missing.
// nullptr when the sprite is null or has no Idle either.
const PpGachiAnim* ppGachiSpriteAnim(const PpGachiSprite* sprite, PpStageAnim anim);

// Fills the stage's timing table for a sprite (all zero frames for nullptr).
void ppGachiSpriteTimings(const PpGachiSprite* sprite, PpStageTimings& out);
