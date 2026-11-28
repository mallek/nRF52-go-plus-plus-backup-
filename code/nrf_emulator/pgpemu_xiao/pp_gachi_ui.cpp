#include "pp_gachi_ui.h"

#if PP_HAS_DISPLAY

#include <math.h>
#include <string.h>

#include "pp_gachi_sprites.h"
#include "pp_gachi_species.h"
#include "pp_gachi_tuning.h"

namespace {

static constexpr int16_t kMargin = 6;
static constexpr int16_t kTopY = 4;          // small top line
static constexpr int16_t kSpriteTop = 16;    // under the top line
static constexpr int16_t kBigChar = 12;      // text size 2: 12 x 16
static constexpr int16_t kSmallChar = 6;     // text size 1: 6 x 8

static constexpr uint16_t kRed = 0xF800;
static constexpr uint16_t kGreen = 0x07E0;
static constexpr uint16_t kWhite = 0xFFFF;
static constexpr uint16_t kBallDark = 0x4208;

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

void rotateAbout(float px, float py, float hx, float hy, float c, float s, int16_t& ox, int16_t& oy)
{
  float dx = px - hx;
  float dy = py - hy;
  ox = static_cast<int16_t>(hx + dx * c - dy * s + 0.5f);
  oy = static_cast<int16_t>(hy + dx * s + dy * c + 0.5f);
}

// An empty, open Poke Ball: red upper half tilted open on a hinge at the right,
// white lower half, a dark band and a button, nothing inside.
void drawEmptyBall(Adafruit_GFX& gfx, int16_t x, int16_t y, int16_t boxW, int16_t boxH, const PpMenuColors& colors)
{
  int16_t r = boxH * 10 / 28;
  int16_t rw = boxW * 10 / 30;
  if (rw < r) {
    r = rw;
  }
  if (r < 6) {
    r = 6;
  }
  int16_t cx = x + boxW / 2 - r / 6;
  int16_t ey = y + (boxH - r * 25 / 10) / 2 + r * 15 / 10;   // equator of the lower half
  int16_t band = r / 6 + 1;

  // Lower half.
  gfx.fillCircle(cx, ey, r, kWhite);
  gfx.fillRect(cx - r - 1, ey - r - 1, 2 * r + 3, r + 1, colors.bg);
  gfx.fillRect(cx - r, ey, 2 * r + 1, band, kBallDark);
  int16_t br = r / 5 + 1;
  gfx.fillCircle(cx, ey + r / 2, br + 2, kBallDark);
  gfx.fillCircle(cx, ey + r / 2, br, kWhite);

  // Upper half, tilted open about the right end of the equator.
  const float th = 0.6f;
  float c = cosf(th);
  float s = sinf(th);
  float hx = static_cast<float>(cx + r);
  float hy = static_cast<float>(ey);
  static constexpr int16_t kSteps = 14;
  int16_t bx = 0;
  int16_t by = 0;
  rotateAbout(static_cast<float>(cx), hy, hx, hy, c, s, bx, by);
  int16_t px = 0;
  int16_t py = 0;
  rotateAbout(static_cast<float>(cx + r), hy, hx, hy, c, s, px, py);
  for (int16_t i = 1; i <= kSteps; ++i) {
    float phi = 3.14159265f * static_cast<float>(i) / static_cast<float>(kSteps);
    int16_t nx = 0;
    int16_t ny = 0;
    rotateAbout(static_cast<float>(cx) + static_cast<float>(r) * cosf(phi), hy - static_cast<float>(r) * sinf(phi), hx, hy, c, s, nx, ny);
    gfx.fillTriangle(bx, by, px, py, nx, ny, kRed);
    gfx.drawLine(px, py, nx, ny, kBallDark);
    px = nx;
    py = ny;
  }
  // Lid rim: a dark band along the lid's base.
  int16_t ax = 0;
  int16_t ay = 0;
  int16_t bx2 = 0;
  int16_t by2 = 0;
  int16_t cx2 = 0;
  int16_t cy2 = 0;
  int16_t dx2 = 0;
  int16_t dy2 = 0;
  rotateAbout(static_cast<float>(cx - r), hy, hx, hy, c, s, ax, ay);
  rotateAbout(static_cast<float>(cx + r), hy, hx, hy, c, s, bx2, by2);
  rotateAbout(static_cast<float>(cx + r), hy - static_cast<float>(band), hx, hy, c, s, cx2, cy2);
  rotateAbout(static_cast<float>(cx - r), hy - static_cast<float>(band), hx, hy, c, s, dx2, dy2);
  gfx.fillTriangle(ax, ay, bx2, by2, cx2, cy2, kBallDark);
  gfx.fillTriangle(ax, ay, cx2, cy2, dx2, dy2, kBallDark);
}

int16_t homeSpriteBoxH(int16_t h)
{
  return ppGachiHomeLinesTop(h) - kSpriteTop - 4;
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

void drawSpriteBoxNumber(Adafruit_GFX& gfx, int16_t x, int16_t y, int16_t boxW, int16_t boxH, uint8_t dex, const PpMenuColors& colors)
{
  int16_t side = minI(boxW, boxH) - 4;
  if (side < 8) {
    return;
  }
  int16_t bx = x + (boxW - side) / 2;
  int16_t by = y + (boxH - side) / 2;
  gfx.drawRoundRect(bx, by, side, side, 8, colors.dim);
  char num[6];
  int16_t pos = 0;
  num[0] = '\0';
  putUint(num, 6, pos, dex);
  uint8_t size = side >= 80 ? 3 : 2;
  int16_t n = cutLen(num, (side - 4) / charW(size));
  printCut(gfx, bx + (side - n * charW(size)) / 2, by + (side - 8 * size) / 2, num, n, size, colors.dim);
}

}  // namespace

int16_t ppGachiHomeLinesTop(int16_t h)
{
  return static_cast<int16_t>(h * 55 / 100 + 2);
}

void ppGachiDrawSprite(Adafruit_GFX& gfx, int16_t x, int16_t y, int16_t boxW, int16_t boxH, uint8_t dex, bool shiny, const PpMenuColors& colors)
{
  const PpGachiSprite* sp = ppGachiSpriteFor(dex, shiny);
  if (sp == nullptr || sp->w == 0 || sp->h == 0 || sp->palette == nullptr || sp->pixels == nullptr) {
    drawSpriteBoxNumber(gfx, x, y, boxW, boxH, dex, colors);
    return;
  }
  int16_t scale = minI(boxW / sp->w, boxH / sp->h);
  if (scale < 1) {
    drawSpriteBoxNumber(gfx, x, y, boxW, boxH, dex, colors);
    return;
  }
  int16_t ox = x + (boxW - sp->w * scale) / 2;
  int16_t oy = y + (boxH - sp->h * scale) / 2;
  int16_t rowBytes = (sp->w + 1) / 2;
  for (int16_t row = 0; row < sp->h; ++row) {
    const uint8_t* line = sp->pixels + row * rowBytes;
    int16_t col = 0;
    while (col < sp->w) {
      uint8_t byte = line[col / 2];
      uint8_t idx = (col % 2 == 0) ? (byte >> 4) : (byte & 0x0F);
      if (idx == 0) {
        ++col;
        continue;
      }
      int16_t start = col;
      ++col;
      while (col < sp->w) {
        uint8_t b2 = line[col / 2];
        uint8_t i2 = (col % 2 == 0) ? (b2 >> 4) : (b2 & 0x0F);
        if (i2 != idx) {
          break;
        }
        ++col;
      }
      gfx.fillRect(ox + start * scale, oy + row * scale, (col - start) * scale, scale, sp->palette[idx]);
    }
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
  gfx.fillScreen(colors.bg);
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

  int16_t boxW = w - 2 * kMargin;
  int16_t boxH = homeSpriteBoxH(h);
  if (st.hasPet) {
    ppGachiDrawSprite(gfx, kMargin, kSpriteTop, boxW, boxH, st.petDex, st.petShiny, colors);
  } else {
    drawEmptyBall(gfx, kMargin, kSpriteTop, boxW, boxH, colors);
  }
  ppGachiDrawHomeLines(gfx, w, h, st, lines, colors);
}

void ppGachiDrawCard(Adafruit_GFX& gfx, int16_t w, int16_t h, const PpGachiCard& card, const PpMenuColors& colors)
{
  gfx.fillScreen(colors.bg);
  gfx.setTextWrap(false);
  int16_t boxY = 10;
  int16_t boxW = w - 2 * kMargin;
  int16_t boxH = h * 55 / 100 - boxY;
  if (card.dex != 0) {
    ppGachiDrawSprite(gfx, kMargin, boxY, boxW, boxH, card.dex, card.shiny, colors);
  } else if (card.emptyBall) {
    drawEmptyBall(gfx, kMargin, boxY, boxW, boxH, colors);
  }
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
