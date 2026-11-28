#pragma once

#include <stdint.h>

// PokeGachi sprite table. The pixel data comes from a generated header made by
// tools/sprites/gen_sprites.py (gitignored). Without it the table is empty and
// every lookup returns nullptr, so the UI draws a placeholder.
struct PpGachiSprite {
  uint8_t dex;
  bool shiny;
  uint8_t w, h;
  const uint16_t* palette;   // 16 RGB565 entries; entry 0 is transparent and never drawn
  const uint8_t* pixels;     // 4 bits per pixel, row by row, high nibble first, each row padded to a whole byte
};

// The sprite for a species; falls back to the normal sprite when the shiny is missing.
// nullptr when the build has no sprite for it (the UI then draws a placeholder).
const PpGachiSprite* ppGachiSpriteFor(uint8_t dex, bool shiny);
