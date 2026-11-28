#include "pp_gachi_sprites.h"

#if defined(__has_include)
#if __has_include("pp_gachi_sprites.generated.h")
#include "pp_gachi_sprites.generated.h"
#define PP_GACHI_HAVE_SPRITES 1
#endif
#endif

#ifndef PP_GACHI_HAVE_SPRITES
static const PpGachiSprite PP_GACHI_SPRITE_TABLE[] = {
  {0, false, 0, 0, nullptr, nullptr},
};
static const uint8_t PP_GACHI_SPRITE_TABLE_COUNT = 0;
#endif

static const PpGachiSprite* findSprite(uint8_t dex, bool shiny) {
  for (uint8_t i = 0; i < PP_GACHI_SPRITE_TABLE_COUNT; i++) {
    const PpGachiSprite& s = PP_GACHI_SPRITE_TABLE[i];
    if (s.dex == dex && s.shiny == shiny) {
      return &s;
    }
  }
  return nullptr;
}

const PpGachiSprite* ppGachiSpriteFor(uint8_t dex, bool shiny) {
  const PpGachiSprite* s = findSprite(dex, shiny);
  if (s == nullptr && shiny) {
    s = findSprite(dex, false);
  }
  return s;
}
