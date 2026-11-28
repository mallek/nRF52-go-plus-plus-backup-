#include "pp_gachi_ui.h"

#if PP_HAS_DISPLAY

#include <string.h>

#include "pp_gachi_sprites.h"
#include "pp_gachi_species.h"
#include "pp_gachi_stage_draw.h"
#include "pp_gachi_tuning.h"

namespace {

static constexpr int16_t kMargin = 6;
static constexpr int16_t kTopY = 4;          // small top line
static constexpr int16_t kBigChar = 12;      // text size 2: 12 x 16
static constexpr int16_t kSmallChar = 6;     // text size 1: 6 x 8
static constexpr int16_t kCardBoxY = 10;     // top of a card's sprite area

static constexpr uint16_t kRed = 0xF800;
static constexpr uint16_t kGreen = 0x07E0;

int16_t charW(uint8_t size)
{
  return static_cast<int16_t>(6 * size);
}

int16_t minI(int16_t a, int16_t b)
{
  return a < b ? a : b;
}

// Prints at most maxChars characters of s, so a long text is cut, never wrapped.
void printCut(Adafruit_GFX& gfx, int16_t x, int16_t y, const char* s, int16_t maxChars, uint8_t size, uint16_t color)
{
  if (maxChars <= 0) {
    return;
  }
  gfx.setTextSize(size);
  gfx.setTextColor(color);
  gfx.setCursor(x, y);
  for (int16_t i = 0; i < maxChars && s[i] != '\0'; ++i) {
    gfx.write(static_cast<uint8_t>(s[i]));
  }
}

int16_t cutLen(const char* s, int16_t maxChars)
{
  int16_t len = static_cast<int16_t>(strlen(s));
  if (maxChars < 0) {
    return 0;
  }
  return len < maxChars ? len : maxChars;
}

// Text size 2 when every given text fits in maxW at that size, else size 1.
uint8_t fitSize(int16_t maxW, const char* a, const char* b)
{
  int16_t la = static_cast<int16_t>(strlen(a));
  int16_t lb = b != nullptr ? static_cast<int16_t>(strlen(b)) : 0;
  int16_t longest = la > lb ? la : lb;
  return (longest * kBigChar <= maxW) ? 2 : 1;
}

// Centred in the width w and cut to maxW.
void printCentered(Adafruit_GFX& gfx, int16_t w, int16_t y, const char* s, uint8_t size, uint16_t color)
{
  int16_t room = w - 2 * kMargin;
  int16_t n = cutLen(s, room / charW(size));
  printCut(gfx, (w - n * charW(size)) / 2, y, s, n, size, color);
}

// Small append helpers for building a line in a fixed buffer.
void putStr(char* buf, int16_t cap, int16_t& pos, const char* s)
{
  while (*s != '\0' && pos < cap - 1) {
    buf[pos++] = *s++;
  }
  buf[pos] = '\0';
}

void putUint(char* buf, int16_t cap, int16_t& pos, unsigned v)
{
  char tmp[6];
  int16_t n = 0;
  do {
    tmp[n++] = static_cast<char>('0' + (v % 10));
    v /= 10;
  } while (v != 0 && n < 5);
  while (n > 0 && pos < cap - 1) {
    buf[pos++] = tmp[--n];
  }
  buf[pos] = '\0';
}

// The empty, open ball centred in a box, sized as in step 1.
void drawBoxedBall(Adafruit_GFX& gfx, int16_t x, int16_t y, int16_t boxW, int16_t boxH)
{
  int16_t r = boxH * 10 / 28;
  int16_t rw = boxW * 10 / 30;
  if (rw < r) {
    r = rw;
  }
  if (r < 6) {
    r = 6;
  }
  int16_t ey = y + (boxH - r * 25 / 10) / 2 + r * 15 / 10;   // equator of the lower half
  ppGachiDrawEmptyBall(gfx, x + boxW / 2, ey + r, r);
}

// Scale and ground point for a sprite in a box: the scale fits every animation in
// `anims` (Idle always), and the Idle frame's crop is centred in the box. Frames of
// other animations stand on the same ground point, so nothing jumps between them.
// False when the sprite cannot be drawn in the box.
bool spriteAnchor(const PpGachiSprite* spr, const PpGachiAnim* other, int16_t x, int16_t y, int16_t boxW, int16_t boxH,
                  uint8_t& scale, int16_t& cx, int16_t& groundY)
{
  const PpGachiAnim* idle = ppGachiSpriteAnim(spr, PP_SANIM_IDLE);
  uint8_t s = ppGachiSpriteScale(spr, boxW, boxH);
  if (idle == nullptr || s == 0) {
    return false;
  }
  if (other != nullptr && other != idle) {
    while (s > 1 && (other->w * s > boxW || other->h * s > boxH)) {
      --s;
    }
  }
  scale = s;
  int16_t left = x + (boxW - idle->w * s) / 2;
  cx = left + idle->originX * s;
  groundY = y + (boxH - idle->h * s) / 2 + idle->baseline * s;
  if (other != nullptr && other != idle) {
    // Keep the other animation inside the box too.
    int16_t topLimit = y + other->baseline * s;
    if (groundY < topLimit) {
      groundY = topLimit;
    }
    if (groundY > y + boxH) {
      groundY = y + boxH;
    }
  }
  return true;
}

// Height of one of the four text rows in the home band.
int16_t homeRowPitch(int16_t h)
{
  return (h - ppGachiHomeLinesTop(h) - 4) / 4;
}

void drawTracks(Adafruit_GFX& gfx, int16_t w, int16_t y, const PpGachiState& st, const PpMenuColors& colors)
{
  uint8_t need = ppGachiNeed(st);
  int16_t trackW = (w - 2 * kMargin) / 3;
  int16_t segPitch = need > 0 ? (trackW - kBigChar - 4 - 5) / need : 0;
  if (segPitch > 10) {
    segPitch = 10;
  }
  int16_t segW = segPitch > 2 ? segPitch - 1 : 1;
  const char letters[3] = {'N', 'H', 'X'};
  const uint8_t values[3] = {st.nutrition, st.happiness, st.experience};
  for (int16_t t = 0; t < 3; ++t) {
    int16_t x0 = kMargin + t * trackW;
    char one[2] = {letters[t], '\0'};
    printCut(gfx, x0, y, one, 1, 2, colors.fg);
    for (uint8_t i = 0; i < need; ++i) {
      int16_t sx = x0 + kBigChar + 4 + i * segPitch;
      if (i < values[t]) {
        gfx.fillRect(sx, y + 2, segW, 12, colors.fg);
      } else {
        gfx.drawRect(sx, y + 2, segW, 12, colors.dim);
      }
    }
  }
}

}  // namespace

int16_t ppGachiHomeLinesTop(int16_t h)
{
  return static_cast<int16_t>(h * 55 / 100 + 2);
}

void ppGachiDrawSprite(Adafruit_GFX& gfx, int16_t x, int16_t y, int16_t boxW, int16_t boxH, uint8_t dex, bool shiny, const PpMenuColors& colors)
{
  const PpGachiSprite* spr = ppGachiSpriteFor(dex, shiny);
  uint8_t scale = 0;
  int16_t cx = 0;
  int16_t groundY = 0;
  if (!spriteAnchor(spr, nullptr, x, y, boxW, boxH, scale, cx, groundY)) {
    ppGachiDrawPlaceholder(gfx, x, y, boxW, boxH, dex, false, colors);
    return;
  }
  ppGachiDrawFrame(gfx, cx, groundY, scale, spr, ppGachiSpriteAnim(spr, PP_SANIM_IDLE), 0, false, false);
}

void ppGachiDrawCardSprite(Adafruit_GFX& gfx, int16_t w, int16_t h, const PpGachiCard& card, uint8_t frame, const PpMenuColors& colors)
{
  int16_t boxW = w - 2 * kMargin;
  int16_t boxH = h * 55 / 100 - kCardBoxY;
  gfx.fillRect(kMargin, kCardBoxY, boxW, boxH, colors.bg);
  if (card.dex == 0) {
    if (card.emptyBall) {
      drawBoxedBall(gfx, kMargin, kCardBoxY, boxW, boxH);
    }
    return;
  }
  // With a berry, the sprite keeps to the left part of the box and the berry stands beside it.
  int16_t berryR = boxH / 12 > 5 ? boxH / 12 : 5;
  int16_t spriteW = card.berry ? boxW - 2 * (2 * berryR + 6) : boxW;
  int16_t spriteX = kMargin + (boxW - spriteW) / 2;
  const PpGachiSprite* spr = ppGachiSpriteFor(card.dex, card.shiny);
  PpStageAnim want = card.spriteAnim < PP_SANIM_COUNT ? static_cast<PpStageAnim>(card.spriteAnim) : PP_SANIM_IDLE;
  const PpGachiAnim* anim = ppGachiSpriteAnim(spr, want);
  uint8_t scale = 0;
  int16_t cx = 0;
  int16_t groundY = 0;
  int16_t halfW = 0;
  if (spriteAnchor(spr, anim, spriteX, kCardBoxY, spriteW, boxH, scale, cx, groundY)) {
    ppGachiDrawFrame(gfx, cx, groundY, scale, spr, anim, frame, false, false);
    const PpGachiAnim* idle = ppGachiSpriteAnim(spr, PP_SANIM_IDLE);
    halfW = static_cast<int16_t>(idle->w * scale / 2);
  } else {
    ppGachiDrawPlaceholder(gfx, spriteX, kCardBoxY, spriteW, boxH, card.dex, false, colors);
    int16_t side = minI(spriteW, boxH) - 4;
    cx = spriteX + spriteW / 2;
    groundY = kCardBoxY + (boxH + side) / 2;
    halfW = side / 2;
  }
  if (card.berry) {
    int16_t bx = cx + halfW + berryR + 4;
    int16_t maxX = kMargin + boxW - berryR - 1;
    if (bx > maxX) {
      bx = maxX;
    }
    ppGachiDrawBerry(gfx, bx, groundY - berryR - 1, berryR);
  }
}

void ppGachiDrawHomeLines(Adafruit_GFX& gfx, int16_t w, int16_t h, const PpGachiState& st, const PpGachiHomeLines& lines, const PpMenuColors& colors)
{
  int16_t top = ppGachiHomeLinesTop(h);
  int16_t pitch = homeRowPitch(h);
  gfx.setTextWrap(false);
  gfx.fillRect(0, top, w, h - top, colors.bg);
  int16_t room = w - 2 * kMargin;
  int16_t row0 = top + 2;
  int16_t row1 = row0 + pitch;
  int16_t row2 = row1 + pitch;
  int16_t row3 = row2 + pitch;

  if (!st.hasPet) {
    const char* a = st.dexRaised == 0 ? "Catch a Pokemon" : "Your next pet is";
    const char* b = st.dexRaised == 0 ? "to meet your first pet" : "one catch away";
    uint8_t size = fitSize(room, a, b);
    printCentered(gfx, w, row0, a, size, colors.fg);
    printCentered(gfx, w, row0 + 8 * size + 6, b, size, colors.dim);
  } else {
    const PpGachiSpecies* sp = ppGachiSpeciesByDex(st.petDex);
    char name[40];
    int16_t pos = 0;
    name[0] = '\0';
    if (sp != nullptr) {
      putStr(name, sizeof(name), pos, sp->name);
      putStr(name, sizeof(name), pos, "  ");
      putUint(name, sizeof(name), pos, sp->stage);
      putStr(name, sizeof(name), pos, "/");
      putUint(name, sizeof(name), pos, sp->stages);
    }
    if (st.petShiny) {
      putStr(name, sizeof(name), pos, " *");
    }
    printCentered(gfx, w, row0, name, fitSize(room, name, nullptr), colors.fg);
    if (ppGachiReady(st)) {
      printCentered(gfx, w, row1 - 4, "READY", 3, colors.accent);
    } else {
      drawTracks(gfx, w, row1, st, colors);
    }
  }

  char pantry[40];
  if (lines.pantryOverride != nullptr) {
    printCentered(gfx, w, row2 + 2, lines.pantryOverride, fitSize(room, lines.pantryOverride, nullptr), colors.fg);
  } else {
    int16_t pos = 0;
    pantry[0] = '\0';
    putStr(pantry, sizeof(pantry), pos, "berries ");
    putUint(pantry, sizeof(pantry), pos, st.berries);
    putStr(pantry, sizeof(pantry), pos, "/");
    putUint(pantry, sizeof(pantry), pos, PP_GACHI_BERRY_CAP);
    putStr(pantry, sizeof(pantry), pos, "  opp ");
    putUint(pantry, sizeof(pantry), pos, st.opponentCount);
    putStr(pantry, sizeof(pantry), pos, "/");
    putUint(pantry, sizeof(pantry), pos, PP_GACHI_OPPONENT_CAP);
    printCentered(gfx, w, row2 + 2, pantry, fitSize(room, pantry, nullptr), colors.fg);
  }
  if (lines.bottom != nullptr) {
    printCentered(gfx, w, row3 + 2, lines.bottom, fitSize(room, lines.bottom, nullptr), lines.bottomColor);
  }
}

void ppGachiDrawHome(Adafruit_GFX& gfx, int16_t w, int16_t h, const PpGachiState& st, const PpGachiHomeLines& lines, const PpMenuColors& colors)
{
  // Clears the top band only; the stage rectangle under it is the caller's, and
  // ppGachiDrawHomeLines clears its own band.
  int16_t stageX = 0;
  int16_t stageY = 0;
  int16_t stageW = 0;
  int16_t stageH = 0;
  ppGachiStageRect(w, h, stageX, stageY, stageW, stageH);
  gfx.fillRect(0, 0, w, stageY, colors.bg);
  gfx.setTextWrap(false);

  // Top line, small: identity on the left, the link word on the right.
  int16_t rightN = lines.topRight != nullptr ? cutLen(lines.topRight, (w - 2 * kMargin) / kSmallChar) : 0;
  if (rightN > 0) {
    printCut(gfx, w - kMargin - rightN * kSmallChar, kTopY, lines.topRight, rightN, 1, lines.topRightColor);
  }
  if (lines.topLeft != nullptr) {
    int16_t leftRoom = (w - 2 * kMargin - rightN * kSmallChar - kSmallChar) / kSmallChar;
    printCut(gfx, kMargin, kTopY, lines.topLeft, cutLen(lines.topLeft, leftRoom), 1, colors.fg);
  }
  ppGachiDrawHomeLines(gfx, w, h, st, lines, colors);
}

void ppGachiDrawCard(Adafruit_GFX& gfx, int16_t w, int16_t h, const PpGachiCard& card, const PpMenuColors& colors)
{
  gfx.fillScreen(colors.bg);
  gfx.setTextWrap(false);
  int16_t boxY = kCardBoxY;
  int16_t boxH = h * 55 / 100 - boxY;
  ppGachiDrawCardSprite(gfx, w, h, card, 0, colors);
  const char* a = card.line1 != nullptr ? card.line1 : "";
  const char* b = card.line2 != nullptr ? card.line2 : "";
  uint8_t size = fitSize(w - 2 * kMargin, a, b);
  int16_t y1 = boxY + boxH + 12;
  printCentered(gfx, w, y1, a, size, colors.fg);
  printCentered(gfx, w, y1 + 8 * size + 10, b, size, colors.dim);
}

void ppGachiDrawBattle(Adafruit_GFX& gfx, int16_t w, int16_t h, const PpGachiBattle& battle, bool roundShowing, uint8_t myDex, bool myShiny, const PpMenuColors& colors)
{
  gfx.fillScreen(colors.bg);
  gfx.setTextWrap(false);

  // Top line: "VS NAME" on the left, the score on the right.
  const PpGachiSpecies* opp = ppGachiSpeciesByDex(battle.opponentDex);
  char title[24];
  int16_t pos = 0;
  title[0] = '\0';
  putStr(title, sizeof(title), pos, "VS ");
  putStr(title, sizeof(title), pos, opp != nullptr ? opp->name : "???");
  char score[24];
  pos = 0;
  score[0] = '\0';
  putStr(score, sizeof(score), pos, "you ");
  putUint(score, sizeof(score), pos, battle.myWins);
  putStr(score, sizeof(score), pos, "  them ");
  putUint(score, sizeof(score), pos, battle.theirWins);
  int16_t room = w - 2 * kMargin;
  int16_t scoreN = cutLen(score, room / kSmallChar);
  int16_t titleLen = static_cast<int16_t>(strlen(title));
  // The score shares the title line when both fit at the large size, else it takes a line of its own.
  bool twoLines = (titleLen * kBigChar + scoreN * kSmallChar + kSmallChar > room);
  uint8_t titleSize = (titleLen * kBigChar <= room) ? 2 : 1;
  printCut(gfx, kMargin, kTopY, title, cutLen(title, room / charW(titleSize)), titleSize, colors.fg);
  int16_t scoreY = twoLines ? kTopY + 8 * titleSize + 4 : kTopY + 4;
  int16_t scoreX = twoLines ? kMargin : w - kMargin - scoreN * kSmallChar;
  printCut(gfx, scoreX, scoreY, score, scoreN, 1, colors.fg);

  // The two sprites side by side.
  int16_t spriteTop = twoLines ? kTopY + 8 * titleSize + 18 : 26;
  int16_t spriteH = h * 38 / 100;
  int16_t colW = (w - 3 * kMargin) / 2;
  int16_t leftX = kMargin;
  int16_t rightX = 2 * kMargin + colW;
  ppGachiDrawSprite(gfx, leftX, spriteTop, colW, spriteH, myDex, myShiny, colors);
  ppGachiDrawSprite(gfx, rightX, spriteTop, colW, spriteH, battle.opponentDex, false, colors);

  // Hints at the bottom, the pick names above them.
  const char* hintL = "A:throw";
  const char* hintR = "B:next";
  int16_t hintChars = static_cast<int16_t>(strlen(hintL) + strlen(hintR) + 1);
  uint8_t hintSize = (hintChars * kBigChar <= w - 2 * kMargin) ? 2 : 1;
  int16_t hintY = h - 8 * hintSize - 4;
  printCut(gfx, kMargin, hintY, hintL, cutLen(hintL, (w / 2) / charW(hintSize)), hintSize, colors.dim);
  int16_t hintRn = cutLen(hintR, (w / 2) / charW(hintSize));
  printCut(gfx, w - kMargin - hintRn * charW(hintSize), hintY, hintR, hintRn, hintSize, colors.dim);

  int16_t cellW = (w - 2 * kMargin) / 3;
  uint8_t pickSize = (5 * kBigChar <= cellW - 4) ? 2 : 1;
  int16_t cellH = 8 * pickSize + 8;
  int16_t rowY = hintY - cellH - 6;
  for (uint8_t i = 0; i < PP_PICK_COUNT; ++i) {
    int16_t cx = kMargin + i * cellW;
    const char* nm = ppGachiPickName(static_cast<PpGachiPick>(i));
    bool on = (battle.cursor == i);
    if (on) {
      gfx.fillRect(cx + 1, rowY, cellW - 2, cellH, colors.accent);
    }
    int16_t n = cutLen(nm, (cellW - 2) / charW(pickSize));
    printCut(gfx, cx + (cellW - n * charW(pickSize)) / 2, rowY + 4, nm, n, pickSize, on ? colors.bg : colors.fg);
  }

  // Both picks and the round result while a thrown round is on screen.
  if (roundShowing) {
    int16_t labelY = spriteTop + spriteH + 4;
    const char* mine = ppGachiPickName(battle.myPick);
    const char* theirs = ppGachiPickName(battle.theirPick);
    uint8_t ls = (5 * kBigChar <= colW) ? 2 : 1;
    int16_t mn = cutLen(mine, colW / charW(ls));
    int16_t tn = cutLen(theirs, colW / charW(ls));
    printCut(gfx, leftX + (colW - mn * charW(ls)) / 2, labelY, mine, mn, ls, colors.fg);
    printCut(gfx, rightX + (colW - tn * charW(ls)) / 2, labelY, theirs, tn, ls, colors.fg);
    const char* word = "TIE";
    uint16_t wordColor = colors.dim;
    if (battle.lastRound == PP_ROUND_WIN) {
      word = "WIN";
      wordColor = kGreen;
    } else if (battle.lastRound == PP_ROUND_LOSE) {
      word = "LOSE";
      wordColor = kRed;
    }
    int16_t wordY = labelY + 8 * ls + 8;
    if (wordY + 24 <= rowY) {
      printCentered(gfx, w, wordY, word, 3, wordColor);
    }
  }
}

#endif  // PP_HAS_DISPLAY
