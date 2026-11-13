#include "pp_anim.h"

#if PP_HAS_DISPLAY

#include <Adafruit_GFX.h>
#include <math.h>

namespace {

// Panel center (120, 120) in canvas coordinates.
static constexpr int16_t kCx = 120;
static constexpr int16_t kCy = 80;

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

static float easeOutQuad(float t)
{
  float u = 1.0f - t;
  return 1.0f - u * u;
}

// Rotates (x, y) about hinge (hx, hy) by `a` radians (screen coords, y down).
static void rotateAbout(float hx, float hy, float a, float& x, float& y)
{
  float dx = x - hx;
  float dy = y - hy;
  float c = cosf(a);
  float sn = sinf(a);
  x = hx + dx * c - dy * sn;
  y = hy + dx * sn + dy * c;
}

// Ball lid: upper half-disc whose flat edge sits on y = cy, hinged at its right
// end and lifted `a` radians, with the black seam stripe along the flat edge.
static void lid(int16_t cx, int16_t cy, int16_t r, float a, uint16_t color)
{
  static constexpr uint8_t kSteps = 20;
  float hx = cx + r;
  float hy = cy;
  float ox = cx;
  float oy = cy;
  rotateAbout(hx, hy, a, ox, oy);
  float px = cx + r;
  float py = cy;
  rotateAbout(hx, hy, a, px, py);
  for (uint8_t i = 1; i <= kSteps; ++i) {
    float t = 3.14159f * i / kSteps;
    float qx = cx + r * cosf(t);
    float qy = cy - r * sinf(t);
    rotateAbout(hx, hy, a, qx, qy);
    canvas->fillTriangle(static_cast<int16_t>(ox), static_cast<int16_t>(oy),
                         static_cast<int16_t>(px), static_cast<int16_t>(py),
                         static_cast<int16_t>(qx), static_cast<int16_t>(qy), color);
    px = qx;
    py = qy;
  }
  // Seam stripe: a 4 px band under the flat edge, rotated with the lid.
  float x0 = cx - r, y0 = cy;
  float x1 = cx + r, y1 = cy;
  float x2 = cx + r, y2 = cy + 4;
  float x3 = cx - r, y3 = cy + 4;
  rotateAbout(hx, hy, a, x0, y0);
  rotateAbout(hx, hy, a, x1, y1);
  rotateAbout(hx, hy, a, x2, y2);
  rotateAbout(hx, hy, a, x3, y3);
  canvas->fillTriangle(x0, y0, x1, y1, x2, y2, ST77XX_BLACK);
  canvas->fillTriangle(x0, y0, x2, y2, x3, y3, ST77XX_BLACK);
}

// Ball drops in with the lid open, the lid snaps shut, the button flashes, then
// big sparkles rise above it, hold, and fade.
static void drawCatch(uint16_t p)
{
  static constexpr int16_t r = 42;
  static constexpr int16_t restY = 112;
  static constexpr float kOpenAngle = 0.85f;   // lid lifted on the left, hinge right

  float fall = easeOut(phase(p, 0, 250));
  float close = phase(p, 250, 420);
  int16_t cy = static_cast<int16_t>(-r + (restY + r) * fall);
  float angle = kOpenAngle * (1.0f - close * close);

  // A small settle bounce right after it closes.
  if (p >= 420 && p < 520) {
    float b = phase(p, 420, 520);
    cy -= static_cast<int16_t>(5.0f * sinf(b * 3.14159f));
  }

  halfDisc(kCx, cy, r, false, ST77XX_WHITE);
  lid(kCx, cy - 4, r, angle, rgb(230, 30, 30));
  canvas->fillRect(kCx - r, cy - 1, 2 * r + 1, 2, ST77XX_BLACK);
  bool flash = (p >= 420 && p < 540) && ((p / 30) % 2 == 0);
  if (close >= 1.0f) {
    canvas->fillCircle(kCx, cy - 2, 12, ST77XX_BLACK);
    canvas->fillCircle(kCx, cy - 2, 8, flash ? rgb(255, 230, 60) : ST77XX_WHITE);
  }

  if (p >= 480) {
    float t = phase(p, 480, 1000);
    float fade = t < 0.6f ? 1.0f : 1.0f - (t - 0.6f) / 0.4f;
    uint8_t level = static_cast<uint8_t>(255.0f * fade);
    uint16_t gold = dim(255, 215, 30, level);
    uint16_t core = dim(255, 255, 220, level);
    static const int16_t xs[4] = { -40, -14, 14, 40 };
    static const int16_t rise[4] = { 30, 44, 40, 28 };
    static const int16_t size[4] = { 9, 12, 11, 9 };
    for (uint8_t i = 0; i < 4; ++i) {
      int16_t x = kCx + xs[i] + static_cast<int16_t>(4.0f * sinf(t * 7.0f + i * 1.7f));
      int16_t y = restY - r - 12 - static_cast<int16_t>(rise[i] * easeOut(t));
      // Twinkle: size pulses a little.
      int16_t s = size[i] + static_cast<int16_t>(2.0f * sinf(t * 18.0f + i));
      sparkle(x, y, s, gold);
      canvas->fillCircle(x, y, 2, core);
    }
  }
}

// Disk flips like a tossed coin, slows to face-on, then item sparks burst out.
static void drawSpin(uint16_t p)
{
  static constexpr int16_t r = 54;
  // 3 half-turns with a gentle ease: the fastest frame steps about 0.55 rad,
  // slow enough at 20 fps to read as a spin instead of flicker.
  static constexpr float kHalfTurns = 3.0f;

  float u = phase(p, 0, 820);
  float theta = kHalfTurns * 3.14159f * easeOutQuad(u);
  int16_t w = static_cast<int16_t>(fabsf(cosf(theta)) * r);
  if (w < 2) {
    w = 2;
  }

  canvas->fillEllipse(kCx, kCy, w, r, rgb(30, 110, 230));
  canvas->drawEllipse(kCx, kCy, w, r, rgb(140, 200, 255));
  // Lighter center band so the flip reads as a disk, not a blob.
  int16_t bw = static_cast<int16_t>(w * 0.55f);
  if (bw > 0) {
    canvas->fillEllipse(kCx, kCy, bw, static_cast<int16_t>(r * 0.55f), rgb(120, 190, 255));
  }
  canvas->fillRect(kCx - w, kCy - 3, 2 * w + 1, 7, rgb(20, 70, 160));

  if (p >= 800) {
    float t = phase(p, 800, 1000);
    uint8_t level = static_cast<uint8_t>(255.0f * (1.0f - t * t));
    static const uint8_t colors[6][3] = {
      { 200, 80, 255 }, { 60, 230, 90 }, { 255, 220, 40 },
      { 255, 90, 90 },  { 90, 220, 255 }, { 255, 160, 40 }
    };
    for (uint8_t i = 0; i < 6; ++i) {
      float a = i * (3.14159f / 3.0f) + 0.5f;
      float dist = r + 4 + 20 * easeOut(t);
      int16_t x = kCx + static_cast<int16_t>(cosf(a) * dist);
      int16_t y = kCy + static_cast<int16_t>(sinf(a) * dist);
      canvas->fillCircle(x, y, 5, dim(colors[i][0], colors[i][1], colors[i][2], level));
    }
  }
}

// Vertical offset for pop-up characters: 0 = fully up, kAnimH = below the
// bottom edge. Rises over 0-300, holds 300-720 (plus `hold`), sinks 720-1000.
static int16_t popOffset(uint16_t p, float hold)
{
  float y;
  if (p < 300) {
    y = kAnimH * (1.0f - easeOut(phase(p, 0, 300)));
  } else if (p < 720) {
    y = hold;
  } else {
    float t = phase(p, 720, 1000);
    y = kAnimH * t * t;
  }
  return static_cast<int16_t>(y);
}

// Thick zigzag lightning bolt with its top at (x, y).
static void bolt(int16_t x, int16_t y, uint16_t color)
{
  static const int8_t pts[5][2] = { { 0, 0 }, { -12, 22 }, { 2, 22 }, { -10, 46 }, { 14, 16 } };
  for (int16_t k = -2; k <= 2; ++k) {
    canvas->drawLine(x + pts[0][0] + k, y + pts[0][1], x + pts[1][0] + k, y + pts[1][1], color);
    canvas->drawLine(x + pts[1][0], y + pts[1][1] + k / 2, x + pts[2][0], y + pts[2][1] + k / 2, color);
    canvas->drawLine(x + pts[2][0] + k, y + pts[2][1], x + pts[3][0] + k, y + pts[3][1], color);
  }
}

// Connected and ready: an electric mouse hops up, cheeks crackling, bolts flash.
static void drawReady(uint16_t p)
{
  const uint16_t yellow = rgb(255, 214, 32);
  const uint16_t tip = rgb(60, 50, 40);
  const uint16_t cheek = rgb(235, 40, 40);
  const uint16_t ink = rgb(25, 20, 15);

  // Two happy hops while up.
  float hop = -fabsf(sinf(phase(p, 300, 720) * 3.14159f * 2.0f)) * 10.0f;
  int16_t d = popOffset(p, hop);

  // Body and paws.
  canvas->fillEllipse(kCx, 200 + d, 64, 58, yellow);
  canvas->fillEllipse(kCx - 30, 166 + d, 12, 9, yellow);
  canvas->fillEllipse(kCx + 30, 166 + d, 12, 9, yellow);
  canvas->drawFastHLine(kCx - 36, 168 + d, 12, rgb(200, 160, 20));
  canvas->drawFastHLine(kCx + 24, 168 + d, 12, rgb(200, 160, 20));

  // Long ears with dark tips (left, then mirrored right).
  for (int8_t side = -1; side <= 1; side += 2) {
    int16_t b1x = kCx + side * 38, b1y = 78 + d;
    int16_t b2x = kCx + side * 18, b2y = 66 + d;
    int16_t tx = kCx + side * 74, ty = 4 + d;
    canvas->fillTriangle(b1x, b1y, b2x, b2y, tx, ty, yellow);
    int16_t p1x = tx + (b1x - tx) * 35 / 100, p1y = ty + (b1y - ty) * 35 / 100;
    int16_t p2x = tx + (b2x - tx) * 35 / 100, p2y = ty + (b2y - ty) * 35 / 100;
    canvas->fillTriangle(tx, ty, p1x, p1y, p2x, p2y, tip);
  }

  // Head.
  canvas->fillEllipse(kCx, 110 + d, 60, 48, yellow);

  // Eyes with highlights.
  canvas->fillCircle(kCx - 24, 102 + d, 9, ink);
  canvas->fillCircle(kCx + 24, 102 + d, 9, ink);
  canvas->fillCircle(kCx - 21, 98 + d, 3, ST77XX_WHITE);
  canvas->fillCircle(kCx + 27, 98 + d, 3, ST77XX_WHITE);
  // Nose and a little w mouth.
  canvas->fillTriangle(kCx - 3, 112 + d, kCx + 3, 112 + d, kCx, 115 + d, ink);
  canvas->drawLine(kCx - 10, 120 + d, kCx - 5, 125 + d, ink);
  canvas->drawLine(kCx - 5, 125 + d, kCx, 120 + d, ink);
  canvas->drawLine(kCx, 120 + d, kCx + 5, 125 + d, ink);
  canvas->drawLine(kCx + 5, 125 + d, kCx + 10, 120 + d, ink);

  // Cheeks crackle: flicker brighter while it is up.
  bool up = p >= 300 && p < 760;
  bool zap = up && ((p / 40) % 2 == 0);
  canvas->fillCircle(kCx - 40, 120 + d, 11, zap ? rgb(255, 90, 90) : cheek);
  canvas->fillCircle(kCx + 40, 120 + d, 11, zap ? rgb(255, 90, 90) : cheek);

  // Lightning bolts on both sides, flashing white and yellow.
  if (up) {
    uint16_t c = zap ? ST77XX_WHITE : yellow;
    bolt(22, 26, c);
    bolt(218, 26, c);
    if (zap) {
      bolt(40, 96, yellow);
      bolt(204, 96, yellow);
    }
  }
}

// Easter egg: a sleepy giant rises from the bottom edge, snoozes a beat, sinks.
static void drawEgg(uint16_t p)
{
  const uint16_t fur = rgb(38, 92, 112);
  const uint16_t furDark = rgb(24, 62, 78);
  const uint16_t cream = rgb(240, 226, 190);
  const uint16_t ink = rgb(20, 30, 36);

  int16_t d = popOffset(p, 2.0f * sinf(phase(p, 300, 720) * 6.2832f * 1.5f));  // slow breathing

  // Arms, body, belly.
  canvas->fillEllipse(30, 168 + d, 26, 40, fur);
  canvas->fillEllipse(210, 168 + d, 26, 40, fur);
  canvas->fillEllipse(kCx, 190 + d, 92, 84, fur);
  canvas->fillEllipse(kCx, 200 + d, 70, 66, cream);

  // Ears, then head.
  canvas->fillTriangle(72, 62 + d, 80, 22 + d, 104, 48 + d, fur);
  canvas->fillTriangle(168, 62 + d, 160, 22 + d, 136, 48 + d, fur);
  canvas->fillTriangle(80, 34 + d, 84, 28 + d, 92, 44 + d, furDark);
  canvas->fillTriangle(160, 34 + d, 156, 28 + d, 148, 44 + d, furDark);
  canvas->fillEllipse(kCx, 84 + d, 58, 48, fur);

  // Face patch with the pointed hairline.
  canvas->fillEllipse(kCx, 98 + d, 44, 32, cream);
  canvas->fillTriangle(kCx - 16, 66 + d, kCx + 16, 66 + d, kCx, 82 + d, fur);

  // Closed eyes, two pixels thick.
  for (int16_t k = 0; k < 2; ++k) {
    canvas->drawFastHLine(kCx - 30, 92 + d + k, 18, ink);
    canvas->drawFastHLine(kCx + 12, 92 + d + k, 18, ink);
  }
  // Content little mouth with two tiny teeth.
  canvas->drawLine(kCx - 12, 110 + d, kCx, 114 + d, ink);
  canvas->drawLine(kCx, 114 + d, kCx + 12, 110 + d, ink);
  canvas->fillTriangle(kCx - 9, 110 + d, kCx - 5, 110 + d, kCx - 7, 106 + d, ST77XX_WHITE);
  canvas->fillTriangle(kCx + 5, 110 + d, kCx + 9, 110 + d, kCx + 7, 106 + d, ST77XX_WHITE);

  // Zzz drifting up while it is up.
  if (p >= 300 && p < 760) {
    float t = phase(p, 300, 760);
    canvas->setTextColor(ST77XX_WHITE);
    canvas->setTextWrap(false);
    static const int16_t zx[3] = { 172, 188, 204 };
    static const int16_t zy[3] = { 52, 30, 8 };
    static const uint8_t zs[3] = { 2, 3, 4 };
    for (uint8_t i = 0; i < 3; ++i) {
      // Each z appears in turn.
      if (t * 3.0f >= i) {
        canvas->setTextSize(zs[i]);
        canvas->setCursor(zx[i], zy[i] + d - static_cast<int16_t>(6.0f * t));
        canvas->print("z");
      }
    }
  }
}

}  // namespace

bool ppAnimDrawFrame(Adafruit_ST7789& tft, PpAnimKind kind, uint16_t progress)
{
  if (canvas == nullptr) {
    canvas = new GFXcanvas16(kAnimW, kAnimH);
  }
  if (canvas == nullptr || canvas->getBuffer() == nullptr) {
    return false;
  }
  canvas->fillScreen(ST77XX_BLACK);
  if (kind == PP_ANIM_CATCH) {
    drawCatch(progress);
  } else if (kind == PP_ANIM_SPIN) {
    drawSpin(progress);
  } else if (kind == PP_ANIM_EGG) {
    drawEgg(progress);
  } else if (kind == PP_ANIM_READY) {
    drawReady(progress);
  }
  tft.drawRGBBitmap(kAnimX, kAnimY, canvas->getBuffer(), kAnimW, kAnimH);
  return true;
}

#endif
