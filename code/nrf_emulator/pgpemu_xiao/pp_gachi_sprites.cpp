#include "pp_gachi_sprites.h"

#if defined(__has_include)
#if __has_include("pp_gachi_sprites.generated.h")
#include "pp_gachi_sprites.generated.h"
#define PP_GACHI_HAVE_SPRITES 1
#endif
#endif

#ifndef PP_GACHI_HAVE_SPRITES
static const PpGachiSprite PP_GACHI_SPRITE_TABLE[] = {
  {0, false, nullptr, {}},
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

static bool usable(const PpGachiAnim& a) {
  return a.frames > 0 && a.w > 0 && a.h > 0 && a.ms != nullptr && a.pixels != nullptr;
}

const PpGachiAnim* ppGachiSpriteAnim(const PpGachiSprite* sprite, PpStageAnim anim) {
  if (sprite == nullptr || sprite->palette == nullptr) {
    return nullptr;
  }
  if (anim < PP_SANIM_COUNT && usable(sprite->anim[anim])) {
    return &sprite->anim[anim];
  }
  if (usable(sprite->anim[PP_SANIM_IDLE])) {
    return &sprite->anim[PP_SANIM_IDLE];
  }
  return nullptr;
}

void ppGachiSpriteTimings(const PpGachiSprite* sprite, PpStageTimings& out) {
  for (uint8_t i = 0; i < PP_SANIM_COUNT; i++) {
    out.anim[i].frames = 0;
    out.anim[i].ms = nullptr;
    if (sprite != nullptr && sprite->palette != nullptr && usable(sprite->anim[i])) {
      out.anim[i].frames = sprite->anim[i].frames;
      out.anim[i].ms = sprite->anim[i].ms;
    }
  }
}
