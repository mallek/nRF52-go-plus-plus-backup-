#include "pp_anim.h"

#if PP_HAS_DISPLAY

#include <Adafruit_GFX.h>
#include <math.h>

namespace {

static constexpr int16_t kCenter = kAnimSize / 2;

static GFXcanvas16* canvas = nullptr;

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Same color dimmed toward black; level 0..255.
static uint16_t dim(uint8_t r, uint8_t g, uint8_t b, uint8_t level)
{
  return rgb(static_cast<uint8_t>((r * level) / 255),
             static_cast<uint8_t>((g * level) / 255),
             static_cast<uint8_t>((b * level) / 255));
}

// 0..1 within [from, to] of progress, clamped.
static float phase(uint16_t progress, uint16_t from, uint16_t to)
{
  if (progress <= from) {
    return 0.0f;
  }
  if (progress >= to) {
    return 1.0f;
  }
  return static_cast<float>(progress - from) / static_cast<float>(to - from);
}

static float easeOut(float t)
{
  float u = 1.0f - t;
  return 1.0f - u * u * u;
}

// Upper (top=true) or lower half of a disc centered at (cx, cy).
static void halfDisc(int16_t cx, int16_t cy, int16_t r, bool top, uint16_t color)
{
  for (int16_t dy = 0; dy <= r; ++dy) {
    int16_t w = static_cast<int16_t>(sqrtf(static_cast<float>(r * r - dy * dy)));
    int16_t y = top ? cy - dy : cy + dy;
    canvas->drawFastHLine(cx - w, y, 2 * w + 1, color);
  }
}

// Four-point sparkle: a long plus and a short x.
static void sparkle(int16_t x, int16_t y, int16_t s, uint16_t color)
{
  canvas->drawFastHLine(x - s, y, 2 * s + 1, color);
  canvas->drawFastVLine(x, y - s, 2 * s + 1, color);
  int16_t d = s / 2;
  canvas->drawLine(x - d, y - d, x + d, y + d, color);
  canvas->drawLine(x - d, y + d, x + d, y - d, color);
  canvas->fillCircle(x, y, 1, color);
}

// Ball drops in open, snaps shut, button flashes, stars rise and fade.
static void drawCatch(uint16_t p)
{
  static constexpr int16_t r = 40;
  static constexpr int16_t restY = 100;
  static constexpr int16_t openGap = 22;

  float fall = easeOut(phase(p, 0, 250));
  float close = phase(p, 250, 450);
  int16_t cy = static_cast<int16_t>(-r + (restY + r) * fall);
  int16_t gap = static_cast<int16_t>(openGap * (1.0f - close * close));

  // A small settle bounce right after it closes.
  if (p >= 450 && p < 550) {
    float b = phase(p, 450, 550);
    cy += static_cast<int16_t>(4.0f * sinf(b * 3.14159f));
  }

  halfDisc(kCenter, cy, r, false, ST77XX_WHITE);
  halfDisc(kCenter, cy - gap, r, true, rgb(230, 30, 30));
  // Seam band and button.
  canvas->fillRect(kCenter - r, cy - gap - 2, 2 * r + 1, gap + 5, ST77XX_BLACK);
  bool flash = (p >= 450 && p < 550) && ((p / 25) % 2 == 0);
  canvas->fillCircle(kCenter, cy - gap / 2, 11, ST77XX_BLACK);
  canvas->fillCircle(kCenter, cy - gap / 2, 7, flash ? rgb(255, 230, 60) : ST77XX_WHITE);

  if (p >= 550) {
    float t = phase(p, 550, 1000);
    uint8_t level = static_cast<uint8_t>(255.0f * (1.0f - t * t));
    uint16_t color = dim(255, 220, 40, level);
    static const int16_t xs[4] = { -34, -11, 13, 36 };
    static const int16_t rise[4] = { 44, 58, 52, 40 };
    for (uint8_t i = 0; i < 4; ++i) {
      int16_t x = kCenter + xs[i] + static_cast<int16_t>(6.0f * sinf(t * 6.0f + i));
      int16_t y = restY - r - 8 - static_cast<int16_t>(rise[i] * easeOut(t));
      sparkle(x, y, (i % 2) ? 5 : 7, color);
    }
  }
}

// Disk flips like a tossed coin, slows to face-on, then item sparks burst out.
static void drawSpin(uint16_t p)
{
  static constexpr int16_t r = 52;
  static constexpr float kHalfTurns = 7.0f;

  float u = phase(p, 0, 800);
  float theta = kHalfTurns * 3.14159f * easeOut(u);
  int16_t w = static_cast<int16_t>(fabsf(cosf(theta)) * r);
  if (w < 2) {
    w = 2;
  }

  canvas->fillEllipse(kCenter, kCenter, w, r, rgb(30, 110, 230));
  canvas->drawEllipse(kCenter, kCenter, w, r, rgb(140, 200, 255));
  // Lighter center band so the flip reads as a disk, not a blob.
  int16_t bw = static_cast<int16_t>(w * 0.55f);
  if (bw > 0) {
    canvas->fillEllipse(kCenter, kCenter, bw, static_cast<int16_t>(r * 0.55f), rgb(120, 190, 255));
  }
  canvas->fillRect(kCenter - w, kCenter - 3, 2 * w + 1, 7, rgb(20, 70, 160));

  if (p >= 800) {
    float t = phase(p, 800, 1000);
    uint8_t level = static_cast<uint8_t>(255.0f * (1.0f - t));
    static const uint8_t colors[6][3] = {
      { 200, 80, 255 }, { 60, 230, 90 }, { 255, 220, 40 },
      { 255, 90, 90 },  { 90, 220, 255 }, { 255, 160, 40 }
    };
    for (uint8_t i = 0; i < 6; ++i) {
      float a = i * (3.14159f / 3.0f) + 0.5f;
      float dist = r + 6 + 26 * easeOut(t);
      int16_t x = kCenter + static_cast<int16_t>(cosf(a) * dist);
      int16_t y = kCenter + static_cast<int16_t>(sinf(a) * dist);
      canvas->fillCircle(x, y, 4, dim(colors[i][0], colors[i][1], colors[i][2], level));
    }
  }
}

}  // namespace

bool ppAnimDrawFrame(Adafruit_ST7789& tft, PpAnimKind kind, uint16_t progress)
{
  if (canvas == nullptr) {
    canvas = new GFXcanvas16(kAnimSize, kAnimSize);
  }
  if (canvas == nullptr || canvas->getBuffer() == nullptr) {
    return false;
  }
  canvas->fillScreen(ST77XX_BLACK);
  if (kind == PP_ANIM_CATCH) {
    drawCatch(progress);
  } else if (kind == PP_ANIM_SPIN) {
    drawSpin(progress);
  }
  tft.drawRGBBitmap(kAnimOrigin, kAnimOrigin, canvas->getBuffer(), kAnimSize, kAnimSize);
  return true;
}

#endif
