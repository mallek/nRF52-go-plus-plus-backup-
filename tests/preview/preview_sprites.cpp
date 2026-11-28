// The real sprite unit, built for the PC preview with one preview-only switch:
// previewHideEat(dex) makes that species' sprite report no Eat animation, so the
// preview can show the feed card of a species without one (the dummy table gives
// every species all five). Everything else is the real lookup code.

#define ppGachiSpriteFor ppGachiSpriteForTable
#include "pp_gachi_sprites.cpp"
#undef ppGachiSpriteFor

#include "preview_sprites.h"

static uint8_t gHideEatDex = 0;

void previewHideEat(uint8_t dex)
{
  gHideEatDex = dex;
}

const PpGachiSprite* ppGachiSpriteFor(uint8_t dex, bool shiny)
{
  const PpGachiSprite* s = ppGachiSpriteForTable(dex, shiny);
  if (s == nullptr || gHideEatDex == 0 || dex != gHideEatDex) {
    return s;
  }
  static PpGachiSprite copy;
  copy = *s;
  copy.anim[PP_SANIM_EAT] = PpGachiAnim();
  return &copy;
}
