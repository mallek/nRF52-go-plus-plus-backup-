#include "pp_gachi_stage_draw.h"

#if PP_HAS_DISPLAY

#include <math.h>

#include "pp_gachi_ui.h"

namespace {

static constexpr int16_t kStageTop = 14;     // under the small top line
static constexpr int16_t kEdge = 2;          // symbols keep this far from the stage edges
static constexpr uint16_t kBlack = 0x0000;
static constexpr uint16_t kWhite = 0xFFFF;
static constexpr uint16_t kRed = 0xF800;
static constexpr uint16_t kBallDark = 0x4208;

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

int16_t maxI(int16_t a, int16_t b)
{
  return a > b ? a : b;
}

int16_t minI(int16_t a, int16_t b)
{
  return a < b ? a : b;
}

int16_t clampI(int16_t v, int16_t lo, int16_t hi)
{
  if (hi < lo) {
    return lo;
  }
  if (v < lo) {
    return lo;
  }
  if (v > hi) {
    return hi;
  }
  return v;
}

// a toward b; t 0..1000.
uint16_t blend(uint16_t a, uint16_t b, int32_t t)
{
  if (t <= 0) {
    return a;
  }
  if (t >= 1000) {
    return b;
  }
  int32_t ar = (a >> 11) & 0x1F;
  int32_t ag = (a >> 5) & 0x3F;
  int32_t ab = a & 0x1F;
  int32_t br = (b >> 11) & 0x1F;
  int32_t bg = (b >> 5) & 0x3F;
  int32_t bb = b & 0x1F;
  int32_t r = ar + (br - ar) * t / 1000;
  int32_t g = ag + (bg - ag) * t / 1000;
  int32_t bl = ab + (bb - ab) * t / 1000;
  return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}

// Where the pet's feet rest: the same fraction of the stage in every scene.
int16_t groundOf(int16_t sh)
{
  return static_cast<int16_t>(sh - sh * 18 / 100);
}

// Top of the ground band; the feet stand a little way into it.
int16_t groundTopOf(int16_t sh)
{
  return static_cast<int16_t>(groundOf(sh) - sh * 8 / 100);
}

// ------------------------------------------------------------------ scenes

// A filled ellipse cut to the stage (0..sw-1, 0..sh-1), so a hill or a canopy that
// runs off the edge of a narrow stage never draws into the rest of the canvas.
void fillEllipseIn(Adafruit_GFX& gfx, int16_t sw, int16_t sh, int16_t cx, int16_t cy, int16_t rx, int16_t ry, uint16_t color)
{
  if (rx <= 0 || ry <= 0) {
    return;
  }
  for (int16_t dy = static_cast<int16_t>(-ry); dy <= ry; ++dy) {
    int16_t y = static_cast<int16_t>(cy + dy);
    if (y < 0 || y >= sh) {
      continue;
    }
    float f = 1.0f - static_cast<float>(dy) * static_cast<float>(dy) / (static_cast<float>(ry) * static_cast<float>(ry));
    int16_t half = static_cast<int16_t>(static_cast<float>(rx) * sqrtf(f > 0.0f ? f : 0.0f) + 0.5f);
    int16_t x0 = maxI(0, static_cast<int16_t>(cx - half));
    int16_t x1 = minI(static_cast<int16_t>(sw - 1), static_cast<int16_t>(cx + half));
    if (x1 >= x0) {
      gfx.drawFastHLine(x0, y, static_cast<int16_t>(x1 - x0 + 1), color);
    }
  }
}

struct Sky {
  uint16_t band[4];
  int16_t bottom;   // the bands cover 0..bottom
};

uint16_t skyAt(const Sky& sky, int16_t y)
{
  if (sky.bottom <= 0) {
    return sky.band[3];
  }
  int16_t i = static_cast<int16_t>(y * 4 / sky.bottom);
  return sky.band[clampI(i, 0, 3)];
}

void drawSky(Adafruit_GFX& gfx, int16_t sw, const Sky& sky)
{
  for (int16_t i = 0; i < 4; ++i) {
    int16_t y0 = static_cast<int16_t>(sky.bottom * i / 4);
    int16_t y1 = static_cast<int16_t>(sky.bottom * (i + 1) / 4);
    gfx.fillRect(0, y0, sw, y1 - y0, sky.band[i]);
  }
}

// A fixed scatter of stars over the sky, the same every time it is drawn.
void drawStars(Adafruit_GFX& gfx, int16_t sw, int16_t yMax, uint8_t count, uint32_t seed)
{
  uint32_t x = seed;
  for (uint8_t i = 0; i < count; ++i) {
    x = x * 1103515245u + 12345u;
    int16_t px = static_cast<int16_t>((x >> 16) % static_cast<uint32_t>(maxI(sw, 1)));
    x = x * 1103515245u + 12345u;
    int16_t py = static_cast<int16_t>((x >> 16) % static_cast<uint32_t>(maxI(yMax, 1)));
    if (i % 5 == 0) {
      gfx.drawFastHLine(px - 1, py, 3, rgb(255, 255, 220));
      gfx.drawFastVLine(px, py - 1, 3, rgb(255, 255, 220));
    } else {
      gfx.drawPixel(px, py, (i % 2 == 0) ? kWhite : rgb(170, 180, 220));
    }
  }
}

int16_t halfChord(int16_t r, int16_t dy)
{
  int32_t q = static_cast<int32_t>(r) * r - static_cast<int32_t>(dy) * dy;
  if (q < 0) {
    return -1;
  }
  return static_cast<int16_t>(sqrtf(static_cast<float>(q)) + 0.5f);
}

// A crescent: the moon's disc less a second disc offset up and to the right. Drawn row
// by row, so the sky behind it is left as it is.
void drawMoon(Adafruit_GFX& gfx, int16_t x, int16_t y, int16_t r)
{
  const uint16_t moon = rgb(245, 240, 200);
  int16_t ox = static_cast<int16_t>(x + r / 2 + 1);
  int16_t oy = static_cast<int16_t>(y - r / 3);
  for (int16_t dy = static_cast<int16_t>(-r); dy <= r; ++dy) {
    int16_t row = static_cast<int16_t>(y + dy);
    int16_t hw = halfChord(r, dy);
    int16_t a = static_cast<int16_t>(x - hw);
    int16_t b = static_cast<int16_t>(x + hw);
    int16_t ow = halfChord(r, static_cast<int16_t>(row - oy));
    if (ow < 0) {
      gfx.drawFastHLine(a, row, static_cast<int16_t>(b - a + 1), moon);
      continue;
    }
    int16_t cutA = static_cast<int16_t>(ox - ow);
    int16_t cutB = static_cast<int16_t>(ox + ow);
    if (cutA > a) {
      gfx.drawFastHLine(a, row, static_cast<int16_t>(minI(b, static_cast<int16_t>(cutA - 1)) - a + 1), moon);
    }
    if (cutB < b) {
      int16_t s = maxI(a, static_cast<int16_t>(cutB + 1));
      gfx.drawFastHLine(s, row, static_cast<int16_t>(b - s + 1), moon);
    }
  }
}

void drawSun(Adafruit_GFX& gfx, int16_t x, int16_t y, int16_t r)
{
  gfx.fillCircle(x, y, r + maxI(2, r / 4), rgb(255, 240, 160));
  gfx.fillCircle(x, y, r, rgb(255, 215, 50));
}

void drawTufts(Adafruit_GFX& gfx, int16_t sw, int16_t top, int16_t bottom, uint16_t color)
{
  int16_t band = bottom - top - 5;
  if (band < 1) {
    return;
  }
  for (int16_t i = 0; i < 14; ++i) {
    int16_t x = static_cast<int16_t>(((i * 37 + 11) % 97) * sw / 97);
    int16_t y = static_cast<int16_t>(top + 5 + (i * 13) % band);
    gfx.drawLine(x, y, x - 2, y - 3, color);
    gfx.drawLine(x, y, x + 2, y - 3, color);
    gfx.drawFastVLine(x, y - 4, 4, color);
  }
}

struct MeadowColors {
  Sky sky;
  uint16_t farHill, nearHill, ground, groundEdge, tuft;
  bool night;
};

void drawMeadow(Adafruit_GFX& gfx, int16_t sw, int16_t sh, const MeadowColors& c)
{
  int16_t gt = groundTopOf(sh);
  drawSky(gfx, sw, c.sky);
  if (c.night) {
    drawStars(gfx, sw, gt - sh / 8, 26, 7u);
    int16_t mr = maxI(5, sh * 8 / 100);
    int16_t mx = sw * 20 / 100;   // on the left, clear of the Zzz that rise to the right
    int16_t my = sh * 22 / 100;
    drawMoon(gfx, mx, my, mr);
  } else {
    drawSun(gfx, sw * 80 / 100, sh * 22 / 100, maxI(5, sh * 9 / 100));
  }
  fillEllipseIn(gfx, sw, sh, sw * 20 / 100, gt, sw * 34 / 100, sh * 20 / 100, c.farHill);
  fillEllipseIn(gfx, sw, sh, sw * 80 / 100, gt, sw * 38 / 100, sh * 15 / 100, c.farHill);
  fillEllipseIn(gfx, sw, sh, sw * 55 / 100, gt + 2, sw * 30 / 100, sh * 9 / 100, c.nearHill);
  gfx.fillRect(0, gt, sw, sh - gt, c.ground);
  gfx.drawFastHLine(0, gt, sw, c.groundEdge);
  drawTufts(gfx, sw, gt, sh, c.tuft);
  if (!c.night) {
    for (int16_t i = 0; i < 7; ++i) {
      int16_t x = static_cast<int16_t>(((i * 53 + 29) % 101) * sw / 101);
      int16_t band = maxI(1, sh - gt - 6);
      int16_t y = static_cast<int16_t>(gt + 4 + (i * 17) % band);
      gfx.fillCircle(x, y, 1, (i % 2 == 0) ? kWhite : rgb(255, 230, 80));
    }
  }
}

const MeadowColors kDayMeadow = {
  {{rgb(70, 140, 230), rgb(100, 165, 240), rgb(135, 190, 245), rgb(175, 215, 250)}, 0},
  rgb(120, 185, 95), rgb(85, 160, 70), rgb(70, 150, 60), rgb(50, 120, 45), rgb(40, 105, 40), false,
};

const MeadowColors kNightMeadow = {
  {{rgb(10, 15, 45), rgb(15, 25, 65), rgb(25, 35, 85), rgb(35, 50, 100)}, 0},
  rgb(28, 60, 55), rgb(22, 50, 42), rgb(20, 52, 36), rgb(14, 38, 26), rgb(12, 32, 22), true,
};

void drawPalm(Adafruit_GFX& gfx, int16_t baseX, int16_t baseY, int16_t height, int16_t sh)
{
  const uint16_t trunk = rgb(140, 95, 50);
  const uint16_t ring = rgb(110, 70, 35);
  const int16_t n = 7;
  int16_t seg = maxI(2, height / n);
  int16_t topX = baseX;
  int16_t topY = baseY;
  for (int16_t k = 0; k < n; ++k) {
    int16_t tw = maxI(3, sh * 5 / 100 - k / 2);
    int16_t x = static_cast<int16_t>(baseX + (k * k) * sh / 300);
    int16_t y = static_cast<int16_t>(baseY - (k + 1) * seg);
    gfx.fillRect(x - tw / 2, y, tw, seg + 1, trunk);
    gfx.drawFastHLine(x - tw / 2, y, tw, ring);
    topX = x;
    topY = y;
  }
  int16_t L = maxI(8, sh * 22 / 100);
  const uint16_t dark = rgb(30, 120, 45);
  const uint16_t light = rgb(45, 155, 60);
  gfx.fillEllipse(topX - L / 2, topY + L / 6, L / 2, maxI(2, L / 6), dark);
  gfx.fillEllipse(topX + L / 2, topY + L / 6, L / 2, maxI(2, L / 6), dark);
  gfx.fillEllipse(topX - L / 3, topY - L / 12, L / 3, maxI(2, L / 7), light);
  gfx.fillEllipse(topX + L / 3, topY - L / 12, L / 3, maxI(2, L / 7), light);
  gfx.fillEllipse(topX, topY - L / 6, maxI(2, L / 7), L / 4, light);
  int16_t cr = maxI(2, L / 9);
  gfx.fillCircle(topX - cr, topY + cr, cr, rgb(100, 65, 30));
  gfx.fillCircle(topX + cr, topY + cr + 1, cr, rgb(90, 58, 28));
}

void drawBeach(Adafruit_GFX& gfx, int16_t sw, int16_t sh, Sky& sky)
{
  int16_t gt = groundTopOf(sh);
  int16_t horizon = static_cast<int16_t>(gt - sh * 16 / 100);
  sky = {{rgb(90, 170, 240), rgb(120, 190, 245), rgb(155, 210, 250), rgb(190, 230, 252)}, horizon};
  drawSky(gfx, sw, sky);
  drawSun(gfx, sw * 78 / 100, sh * 20 / 100, maxI(5, sh * 8 / 100));
  int16_t mid = static_cast<int16_t>(horizon + (gt - horizon) / 2);
  gfx.fillRect(0, horizon, sw, mid - horizon, rgb(30, 110, 200));
  gfx.fillRect(0, mid, sw, gt - mid, rgb(50, 140, 215));
  const uint16_t wave = rgb(200, 230, 250);
  for (int16_t i = 0; i < 9; ++i) {
    int16_t x = static_cast<int16_t>(((i * 41 + 7) % 89) * sw / 89);
    int16_t y = static_cast<int16_t>(horizon + 2 + (i * 5) % maxI(1, gt - horizon - 3));
    gfx.drawFastHLine(x, y, maxI(4, sw / 24), wave);
  }
  gfx.fillRect(0, gt, sw, sh - gt, rgb(235, 210, 150));
  gfx.drawFastHLine(0, gt, sw, rgb(210, 180, 120));
  gfx.drawFastHLine(0, gt + 1, sw, rgb(220, 192, 132));
  for (int16_t i = 0; i < 10; ++i) {
    int16_t x = static_cast<int16_t>(((i * 61 + 13) % 103) * sw / 103);
    int16_t y = static_cast<int16_t>(gt + 4 + (i * 7) % maxI(1, sh - gt - 5));
    gfx.drawPixel(x, y, rgb(200, 170, 110));
  }
  drawPalm(gfx, sw * 9 / 100, static_cast<int16_t>(gt + sh * 4 / 100), static_cast<int16_t>(sh * 50 / 100), sh);
}

void drawForest(Adafruit_GFX& gfx, int16_t sw, int16_t sh, Sky& sky)
{
  int16_t gt = groundTopOf(sh);
  sky = {{rgb(40, 85, 55), rgb(32, 72, 46), rgb(26, 62, 40), rgb(22, 54, 34)}, gt};
  drawSky(gfx, sw, sky);
  const uint16_t far = rgb(55, 45, 32);
  const uint16_t near = rgb(95, 65, 40);
  const uint16_t nearEdge = rgb(70, 48, 30);
  const int16_t farX[3] = {15, 47, 80};
  for (int16_t i = 0; i < 3; ++i) {
    int16_t tw = maxI(3, sw * 3 / 100);
    gfx.fillRect(sw * farX[i] / 100 - tw / 2, 0, tw, gt, far);
  }
  const int16_t nearX[4] = {4, 31, 64, 94};
  for (int16_t i = 0; i < 4; ++i) {
    int16_t tw = maxI(5, sw * 6 / 100);
    int16_t x = static_cast<int16_t>(sw * nearX[i] / 100 - tw / 2);
    gfx.fillRect(x, 0, tw, gt + 3, near);
    gfx.drawFastVLine(x + tw - 1, 0, gt + 3, nearEdge);
  }
  int16_t r = maxI(6, sh * 20 / 100);
  const uint16_t canopyDark = rgb(25, 90, 40);
  const uint16_t canopy = rgb(35, 112, 50);
  const uint16_t canopyLight = rgb(55, 135, 62);
  for (int16_t i = 0; i <= 6; ++i) {
    int16_t x = static_cast<int16_t>(i * sw / 6);
    fillEllipseIn(gfx, sw, sh, x, static_cast<int16_t>(sh * 6 / 100 + (i % 2) * r / 3), r, r, canopyDark);
  }
  for (int16_t i = 0; i < 6; ++i) {
    int16_t x = static_cast<int16_t>(i * sw / 6 + sw / 12);
    fillEllipseIn(gfx, sw, sh, x, static_cast<int16_t>(sh * 2 / 100), r * 3 / 4, r * 3 / 4, canopy);
    fillEllipseIn(gfx, sw, sh, x - r / 4, static_cast<int16_t>(sh * 2 / 100 - r / 4), r / 3, r / 3, canopyLight);
  }
  gfx.fillRect(0, gt, sw, sh - gt, rgb(70, 85, 40));
  gfx.drawFastHLine(0, gt, sw, rgb(52, 64, 30));
  fillEllipseIn(gfx, sw, sh, sw * 18 / 100, gt, sw * 9 / 100, sh * 7 / 100, rgb(35, 105, 45));
  fillEllipseIn(gfx, sw, sh, sw * 78 / 100, gt, sw * 11 / 100, sh * 8 / 100, rgb(35, 105, 45));
  gfx.fillRect(0, gt + 1, sw, 1, rgb(60, 74, 35));
  drawTufts(gfx, sw, gt, sh, rgb(50, 66, 28));
}

void drawCamp(Adafruit_GFX& gfx, int16_t sw, int16_t sh, Sky& sky)
{
  int16_t gt = groundTopOf(sh);
  sky = {{rgb(8, 10, 35), rgb(12, 16, 48), rgb(18, 24, 62), rgb(26, 32, 75)}, gt};
  drawSky(gfx, sw, sky);
  drawStars(gfx, sw, gt - sh / 8, 30, 21u);
  int16_t mr = maxI(5, sh * 7 / 100);
  int16_t my = sh * 18 / 100;
  drawMoon(gfx, sw * 74 / 100, my, mr);
  gfx.fillRect(0, gt, sw, sh - gt, rgb(38, 42, 30));
  gfx.drawFastHLine(0, gt, sw, rgb(28, 32, 24));
  drawTufts(gfx, sw, gt, sh, rgb(26, 30, 20));

  // Tent on the left.
  int16_t tl = sw * 3 / 100;
  int16_t tr = sw * 29 / 100;
  int16_t tb = static_cast<int16_t>(gt + sh * 3 / 100);
  int16_t tx = (tl + tr) / 2;
  int16_t ty = static_cast<int16_t>(gt - sh * 24 / 100);
  gfx.fillTriangle(tl, tb, tr, tb, tx, ty, rgb(160, 85, 40));
  gfx.fillTriangle(tx, ty, tr, tb, tx + (tr - tx) / 3, tb, rgb(125, 65, 30));
  gfx.fillTriangle(tx - (tr - tl) / 7, tb, tx + (tr - tl) / 7, tb, tx, ty + (tb - ty) * 2 / 5, rgb(45, 25, 12));

  // Small fire on the right.
  int16_t fx = sw * 87 / 100;
  int16_t fy = static_cast<int16_t>(groundOf(sh) - 1);
  int16_t fw = maxI(4, sh * 6 / 100);
  int16_t fh = maxI(8, sh * 15 / 100);
  gfx.fillEllipse(fx, fy, fw * 2, maxI(2, fw / 2), rgb(85, 60, 32));
  const uint16_t log = rgb(110, 70, 35);
  for (int16_t k = -1; k <= 1; ++k) {
    gfx.drawLine(fx - fw - 2, fy + k, fx + fw + 2, fy - 3 + k, log);
    gfx.drawLine(fx - fw - 2, fy - 3 + k, fx + fw + 2, fy + k, log);
  }
  gfx.fillTriangle(fx - fw, fy - 2, fx + fw, fy - 2, fx, fy - 2 - fh, rgb(255, 120, 20));
  gfx.fillTriangle(fx - fw, fy - 2, fx - fw / 3, fy - 2, fx - fw + 1, fy - 2 - fh / 2, rgb(255, 120, 20));
  gfx.fillTriangle(fx + fw / 3, fy - 2, fx + fw, fy - 2, fx + fw - 1, fy - 2 - fh * 3 / 5, rgb(255, 120, 20));
  gfx.fillTriangle(fx - fw / 2, fy - 2, fx + fw / 2, fy - 2, fx, fy - 2 - fh * 3 / 5, rgb(255, 220, 60));
}

struct SceneLook {
  Sky sky;
  uint16_t shadow;
};

SceneLook drawScene(Adafruit_GFX& gfx, int16_t sw, int16_t sh, PpStageScene scene)
{
  SceneLook look;
  int16_t gt = groundTopOf(sh);
  switch (scene) {
    case PP_SCENE_BEACH:
      drawBeach(gfx, sw, sh, look.sky);
      look.shadow = rgb(200, 172, 115);
      break;
    case PP_SCENE_FOREST:
      drawForest(gfx, sw, sh, look.sky);
      look.shadow = rgb(48, 60, 28);
      break;
    case PP_SCENE_CAMP_NIGHT:
      drawCamp(gfx, sw, sh, look.sky);
      look.shadow = rgb(22, 25, 18);
      break;
    case PP_SCENE_MEADOW_NIGHT: {
      MeadowColors c = kNightMeadow;
      c.sky.bottom = gt;
      drawMeadow(gfx, sw, sh, c);
      look.sky = c.sky;
      look.shadow = rgb(10, 30, 20);
      break;
    }
    case PP_SCENE_MEADOW:
    default: {
      MeadowColors c = kDayMeadow;
      c.sky.bottom = gt;
      drawMeadow(gfx, sw, sh, c);
      look.sky = c.sky;
      look.shadow = rgb(48, 112, 42);
      break;
    }
  }
  return look;
}

// ------------------------------------------------------------------ sprite helpers

uint8_t pixelAt(const PpGachiAnim& a, uint8_t frame, int16_t col, int16_t row)
{
  int16_t rowBytes = static_cast<int16_t>((a.w + 1) / 2);
  const uint8_t* base = a.pixels + static_cast<uint32_t>(frame) * a.h * rowBytes;
  uint8_t byte = base[row * rowBytes + col / 2];
  return (col % 2 == 0) ? static_cast<uint8_t>(byte >> 4) : static_cast<uint8_t>(byte & 0x0F);
}

bool rowEmpty(const PpGachiAnim& a, uint8_t frame, int16_t row)
{
  int16_t rowBytes = static_cast<int16_t>((a.w + 1) / 2);
  const uint8_t* line = a.pixels + static_cast<uint32_t>(frame) * a.h * rowBytes + row * rowBytes;
  for (int16_t i = 0; i < rowBytes; ++i) {
    if (line[i] != 0) {
      return false;
    }
  }
  return true;
}

// First drawn row, and one past the last drawn row, of a frame. Both 0 for an empty frame.
void frameRows(const PpGachiAnim& a, uint8_t frame, int16_t& top, int16_t& bottom)
{
  top = 0;
  bottom = 0;
  int16_t t = 0;
  while (t < a.h && rowEmpty(a, frame, t)) {
    ++t;
  }
  if (t >= a.h) {
    return;
  }
  int16_t b = a.h;
  while (b > t && rowEmpty(a, frame, static_cast<int16_t>(b - 1))) {
    --b;
  }
  top = t;
  bottom = b;
}

// How far down to move a frame so the tallest frame of its animation (a high hop) stays
// inside the stage, under a top margin of a tenth of the stage. Frames on the ground are
// not moved; airborne frames are lowered in proportion to their height, so the motion is
// kept, only flattened.
int16_t keepInsideShift(const PpGachiAnim& a, uint8_t frame, uint8_t scale, int16_t groundY, int16_t sh)
{
  int32_t extent = static_cast<int32_t>(a.baseline) * scale;
  int32_t limit = groundY - maxI(kEdge, static_cast<int16_t>(sh / 10));
  if (extent <= limit || a.frames == 0) {
    return 0;
  }
  int32_t excess = extent - limit;
  int32_t liftMax = 0;
  int32_t lift = 0;
  for (uint8_t f = 0; f < a.frames; ++f) {
    int16_t top = 0;
    int16_t bottom = 0;
    frameRows(a, f, top, bottom);
    int32_t l = bottom > 0 ? static_cast<int32_t>(a.baseline) - bottom : 0;
    if (l > liftMax) {
      liftMax = l;
    }
    if (f == frame) {
      lift = l;
    }
  }
  if (liftMax <= 0 || lift <= 0) {
    return 0;
  }
  int32_t shift = lift * excess / liftMax;
  int32_t most = lift * scale;
  return static_cast<int16_t>(shift < most ? shift : most);
}

// One scale per species for every animation: the Idle frame at about 60% of the stage
// height, made smaller only when the pet would not fit across the stage (walking to the
// ends of its span) or its Idle frame would not fit above the ground line.
uint8_t petScale(const PpGachiSprite* sprite, int16_t sw, int16_t sh, int16_t groundY)
{
  const PpGachiAnim* idle = ppGachiSpriteAnim(sprite, PP_SANIM_IDLE);
  if (idle == nullptr) {
    return 0;
  }
  uint8_t s = ppGachiSpriteScale(sprite, sw, static_cast<int16_t>(sh * 60 / 100));
  if (s == 0) {
    s = 1;
  }
  int16_t span = static_cast<int16_t>(PP_STAGE_WALK_SPAN * sw / 240);
  for (uint8_t i = 0; i < PP_SANIM_COUNT; ++i) {
    const PpGachiAnim* a = ppGachiSpriteAnim(sprite, static_cast<PpStageAnim>(i));
    if (a == nullptr) {
      continue;
    }
    int16_t half = maxI(a->originX, static_cast<int16_t>(a->w - a->originX));
    int16_t room = static_cast<int16_t>(sw / 2 - (i == PP_SANIM_WALK ? span : 0));
    while (s > 1 && half * s > room) {
      --s;
    }
  }
  while (s > 1 && idle->baseline * s > groundY - kEdge) {
    --s;
  }
  return s;
}

// ------------------------------------------------------------------ symbols

void drawHeart(Adafruit_GFX& gfx, int16_t cx, int16_t cy, int16_t r, uint16_t color)
{
  gfx.fillCircle(cx - r, cy, r, color);
  gfx.fillCircle(cx + r, cy, r, color);
  gfx.fillTriangle(cx - 2 * r, cy + 1, cx + 2 * r, cy + 1, cx, cy + 2 * r + r / 2, color);
}

void drawSparkle(Adafruit_GFX& gfx, int16_t x, int16_t y, int16_t r, uint16_t color)
{
  int16_t t = maxI(1, r / 3);
  gfx.fillTriangle(x - t, y, x + t, y, x, y - r, color);
  gfx.fillTriangle(x - t, y, x + t, y, x, y + r, color);
  gfx.fillTriangle(x, y - t, x, y + t, x - r, y, color);
  gfx.fillTriangle(x, y - t, x, y + t, x + r, y, color);
  gfx.drawPixel(x, y, kWhite);
}

// Text with a one-pixel dark shadow, so it reads on light and dark scenes.
void shadowText(Adafruit_GFX& gfx, int16_t x, int16_t y, const char* s, uint8_t size, uint16_t color)
{
  gfx.setTextWrap(false);
  gfx.setTextSize(size);
  gfx.setTextColor(kBlack);
  gfx.setCursor(x + 1, y + 1);
  gfx.print(s);
  gfx.setTextColor(color);
  gfx.setCursor(x, y);
  gfx.print(s);
}

void drawExclamation(Adafruit_GFX& gfx, int16_t x, int16_t top, int16_t height)
{
  int16_t wd = maxI(4, height / 4);
  int16_t bar = height * 65 / 100;
  int16_t dotR = wd / 2;
  int16_t dotY = static_cast<int16_t>(top + height - dotR);
  gfx.fillRoundRect(x - wd / 2 - 1, top - 1, wd + 2, bar + 2, wd / 2 + 1, kWhite);
  gfx.fillCircle(x, dotY, dotR + 1, kWhite);
  gfx.fillRoundRect(x - wd / 2, top, wd, bar, wd / 2, kRed);
  gfx.fillCircle(x, dotY, dotR, kRed);
}

struct PetSpot {
  int16_t cx;        // centre of the pet, after dx
  int16_t groundY;   // the ground line, after dy
  int16_t headY;     // top of the standing pet (Idle frame 0)
  int16_t halfW;     // half the pet's width
  int16_t frameTop;  // top of what is drawn this frame
  int16_t tippedH;   // height of the pet lying on its side
  int16_t tippedW;   // width of the pet lying on its side
};

void drawSymbol(Adafruit_GFX& gfx, int16_t sw, int16_t sh, const PpStagePose& pose, const PetSpot& pet, const Sky& sky)
{
  int32_t p = pose.symbolProgress > 1000 ? 1000 : pose.symbolProgress;
  int16_t groundY = groundOf(sh);
  switch (pose.symbol) {
    case PP_SYM_HEARTS: {
      // Three hearts, left, right, left, rising past the sides of the head so a hopping
      // pet never covers them.
      int16_t r = maxI(3, sh / 30);
      for (int16_t i = 0; i < 3; ++i) {
        int32_t t = (p - i * 200) * 1000 / 600;
        if (t <= 0 || t >= 1000) {
          continue;
        }
        int16_t side = (i % 2 == 0) ? -1 : 1;
        int16_t off = static_cast<int16_t>(side * (pet.halfW + 3 * r));
        int16_t startY = static_cast<int16_t>(pet.headY + pet.halfW / 3);
        int16_t topY = static_cast<int16_t>(r + kEdge);
        int16_t rise = maxI(0, startY - topY);
        int16_t y = static_cast<int16_t>(startY - rise * t / 1000);
        int16_t x = static_cast<int16_t>(pet.cx + off + static_cast<int16_t>(sinf(t * 0.0126f) * r / 2));
        x = clampI(x, 2 * r + kEdge, sw - 2 * r - kEdge - 1);
        y = clampI(y, r + kEdge, sh - 3 * r - kEdge);
        uint16_t color = blend(rgb(250, 60, 110), skyAt(sky, y), t > 500 ? (t - 500) * 2 : 0);
        drawHeart(gfx, x, y, r, color);
      }
      break;
    }
    case PP_SYM_STARS: {
      int16_t hx = pet.cx;
      int16_t hy = static_cast<int16_t>(pet.headY - 3);
      int16_t rx = maxI(10, pet.halfW * 7 / 10);
      if (pose.tipped) {
        int16_t side = pose.mirror ? -1 : 1;
        hx = static_cast<int16_t>(pet.cx + side * pet.tippedW / 4);
        hy = static_cast<int16_t>(pet.groundY - pet.tippedH - 3);
        rx = maxI(10, pet.tippedW * 4 / 10);
      }
      int16_t ry = maxI(3, rx / 3);
      int16_t sr = maxI(3, sh / 30);
      hx = clampI(hx, rx + sr + kEdge, sw - rx - sr - kEdge - 1);
      hy = clampI(hy, ry + sr + kEdge, sh - ry - sr - kEdge - 1);
      for (int16_t i = 0; i < 3; ++i) {
        float a = static_cast<float>(p) * 0.012566f + static_cast<float>(i) * 2.0944f;
        int16_t x = static_cast<int16_t>(hx + cosf(a) * rx);
        int16_t y = static_cast<int16_t>(hy + sinf(a) * ry);
        drawSparkle(gfx, x, y, sr, rgb(255, 230, 40));
      }
      break;
    }
    case PP_SYM_BERRY:
    case PP_SYM_BERRY_BOUNCE: {
      int16_t r = maxI(4, sh / 18);
      int16_t restY = static_cast<int16_t>(groundY - r);
      int16_t startY = static_cast<int16_t>(r + kEdge);
      int16_t bx = clampI(static_cast<int16_t>(pet.cx + pet.halfW + r + 4), r + kEdge, sw - r - kEdge - 1);
      bool bounce = (pose.symbol == PP_SYM_BERRY_BOUNCE);
      int32_t fallEnd = bounce ? 300 : 350;
      int16_t x = bx;
      int16_t y = restY;
      int16_t rr = r;
      if (p < fallEnd) {
        int32_t t = p * 1000 / fallEnd;
        y = static_cast<int16_t>(startY + (restY - startY) * t * t / 1000000);
      } else if (bounce) {
        int16_t edgeX = static_cast<int16_t>(sw - r - kEdge - 1);
        if (p < 800) {
          int32_t u = (p - fallEnd) * 1000 / (800 - fallEnd);
          x = static_cast<int16_t>(bx + (edgeX - bx) * u / 1000);
          int32_t local = u < 600 ? u * 1000 / 600 : (u - 600) * 1000 / 400;
          int16_t hgt = static_cast<int16_t>(u < 600 ? sh * 28 / 100 : sh * 12 / 100);
          y = static_cast<int16_t>(restY - hgt * sinf(static_cast<float>(local) * 0.0031416f));
        } else {
          x = edgeX;
          rr = static_cast<int16_t>(r * (1000 - p) / 200);
        }
        y = clampI(y, r + kEdge, restY);
      }
      if (rr > 0) {
        ppGachiDrawBerry(gfx, x, y, rr);
      }
      if (bounce && p >= fallEnd) {
        const char* word = "full";
        int16_t tw = 4 * 12;
        int16_t ty = clampI(static_cast<int16_t>(pet.headY - 22), kEdge, sh - 16 - kEdge);
        int16_t tx = clampI(static_cast<int16_t>(pet.cx - tw / 2), kEdge, sw - tw - kEdge);
        shadowText(gfx, tx, ty, word, 2, rgb(255, 165, 0));
      }
      break;
    }
    case PP_SYM_ALERT: {
      int16_t height = maxI(14, sh * 18 / 100);
      if (p < 100) {
        height = static_cast<int16_t>(maxI(4, height * (p + 30) / 130));
      }
      int16_t top = static_cast<int16_t>(pet.headY - 6 - height);
      top = clampI(top, kEdge + 1, sh - height - kEdge - 1);
      drawExclamation(gfx, clampI(pet.cx, 6, sw - 7), top, height);
      break;
    }
    case PP_SYM_ZZZ: {
      int16_t x0 = static_cast<int16_t>(pet.cx + pet.halfW / 3);
      int16_t y0 = static_cast<int16_t>(pet.frameTop - 10);
      int16_t rangeX = maxI(8, pet.halfW);
      for (int16_t i = 0; i < 3; ++i) {
        int32_t t = (p + i * 333) % 1000;
        uint8_t size = static_cast<uint8_t>(1 + t * 3 / 1000);
        int16_t top = static_cast<int16_t>(kEdge);
        int16_t rise = maxI(0, y0 - top);
        int16_t y = static_cast<int16_t>(y0 - rise * t / 1000);
        int16_t x = static_cast<int16_t>(x0 + rangeX * t / 1000);
        x = clampI(x, kEdge, sw - 6 * size - kEdge);
        y = clampI(y, kEdge, sh - 8 * size - kEdge);
        shadowText(gfx, x, y, "Z", size, kWhite);
      }
      break;
    }
    case PP_SYM_NONE:
    default:
      break;
  }
}

void putDex(char* buf, uint8_t v)
{
  char tmp[4];
  int16_t n = 0;
  do {
    tmp[n++] = static_cast<char>('0' + (v % 10));
    v = static_cast<uint8_t>(v / 10);
  } while (v != 0 && n < 3);
  int16_t pos = 0;
  while (n > 0) {
    buf[pos++] = tmp[--n];
  }
  buf[pos] = '\0';
}

}  // namespace

void ppGachiStageRect(int16_t w, int16_t h, int16_t& x, int16_t& y, int16_t& sw, int16_t& sh)
{
  x = 0;
  y = kStageTop;
  sw = w;
  sh = static_cast<int16_t>(ppGachiHomeLinesTop(h) - kStageTop);
  if (sh < 0) {
    sh = 0;
  }
}

uint8_t ppGachiSpriteScale(const PpGachiSprite* sprite, int16_t boxW, int16_t boxH)
{
  const PpGachiAnim* idle = ppGachiSpriteAnim(sprite, PP_SANIM_IDLE);
  if (idle == nullptr || boxW <= 0 || boxH <= 0) {
    return 0;
  }
  int16_t s = minI(static_cast<int16_t>(boxW / idle->w), static_cast<int16_t>(boxH / idle->h));
  if (s < 1) {
    return 0;
  }
  return static_cast<uint8_t>(s > 255 ? 255 : s);
}

void ppGachiDrawFrame(Adafruit_GFX& gfx, int16_t cx, int16_t groundY, uint8_t scale,
                      const PpGachiSprite* sprite, const PpGachiAnim* anim, uint8_t frame, bool mirror, bool tipped)
{
  if (sprite == nullptr || sprite->palette == nullptr || anim == nullptr || anim->frames == 0 ||
      anim->pixels == nullptr || anim->w == 0 || anim->h == 0 || scale == 0) {
    return;
  }
  const PpGachiAnim& a = *anim;
  uint8_t f = static_cast<uint8_t>(frame % a.frames);
  int16_t s = scale;
  int16_t rows = minI(a.h, a.baseline > 0 ? a.baseline : a.h);
  for (int16_t row = 0; row < rows; ++row) {
    int16_t col = 0;
    while (col < a.w) {
      uint8_t idx = pixelAt(a, f, col, row);
      if (idx == 0) {
        ++col;
        continue;
      }
      int16_t start = col;
      ++col;
      while (col < a.w && pixelAt(a, f, col, row) == idx) {
        ++col;
      }
      uint16_t color = sprite->palette[idx];
      int16_t run = static_cast<int16_t>(col - start);
      if (!tipped) {
        // Upright: row `row` sits (baseline - row) rows above the ground; columns about originX.
        int16_t y = static_cast<int16_t>(groundY - (a.baseline - row) * s);
        int16_t x = mirror ? static_cast<int16_t>(cx + (a.originX - col) * s)
                           : static_cast<int16_t>(cx + (start - a.originX) * s);
        gfx.fillRect(x, y, run * s, s, color);
      } else {
        // On its side: a quarter turn, head toward the facing side, the frame's left
        // edge resting on the ground. Source rows become columns about the body's middle.
        int16_t colOut = mirror ? static_cast<int16_t>(row - rows / 2) : static_cast<int16_t>(rows - 1 - row - rows / 2);
        int16_t x = static_cast<int16_t>(cx + colOut * s);
        int16_t y = static_cast<int16_t>(groundY - (a.w - start) * s);
        gfx.fillRect(x, y, s, run * s, color);
      }
    }
  }
}

void ppGachiDrawEmptyBall(Adafruit_GFX& gfx, int16_t cx0, int16_t groundY, int16_t r)
{
  if (r < 6) {
    r = 6;
  }
  int16_t cx = static_cast<int16_t>(cx0 - r / 6);
  int16_t ey = static_cast<int16_t>(groundY - r);   // equator of the lower half
  int16_t band = static_cast<int16_t>(r / 6 + 1);

  // Lower half, a filled half disc.
  for (int16_t dy = 0; dy <= r; ++dy) {
    int16_t half = static_cast<int16_t>(sqrtf(static_cast<float>(r * r - dy * dy)) + 0.5f);
    gfx.drawFastHLine(cx - half, ey + dy, 2 * half + 1, kWhite);
  }
  gfx.fillRect(cx - r, ey, 2 * r + 1, band, kBallDark);
  int16_t br = static_cast<int16_t>(r / 5 + 1);
  gfx.fillCircle(cx, ey + r / 2, br + 2, kBallDark);
  gfx.fillCircle(cx, ey + r / 2, br, kWhite);

  // Upper half, tilted open about the right end of the equator.
  const float th = 0.6f;
  const float c = cosf(th);
  const float s = sinf(th);
  const float hx = static_cast<float>(cx + r);
  const float hy = static_cast<float>(ey);
  auto rot = [&](float px, float py, int16_t& ox, int16_t& oy) {
    float dx = px - hx;
    float dy = py - hy;
    ox = static_cast<int16_t>(hx + dx * c - dy * s + 0.5f);
    oy = static_cast<int16_t>(hy + dx * s + dy * c + 0.5f);
  };
  static constexpr int16_t kSteps = 14;
  int16_t bx = 0;
  int16_t by = 0;
  rot(static_cast<float>(cx), hy, bx, by);
  int16_t px = 0;
  int16_t py = 0;
  rot(static_cast<float>(cx + r), hy, px, py);
  for (int16_t i = 1; i <= kSteps; ++i) {
    float phi = 3.14159265f * static_cast<float>(i) / static_cast<float>(kSteps);
    int16_t nx = 0;
    int16_t ny = 0;
    rot(static_cast<float>(cx) + static_cast<float>(r) * cosf(phi), hy - static_cast<float>(r) * sinf(phi), nx, ny);
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
  rot(static_cast<float>(cx - r), hy, ax, ay);
  rot(static_cast<float>(cx + r), hy, bx2, by2);
  rot(static_cast<float>(cx + r), hy - static_cast<float>(band), cx2, cy2);
  rot(static_cast<float>(cx - r), hy - static_cast<float>(band), dx2, dy2);
  gfx.fillTriangle(ax, ay, bx2, by2, cx2, cy2, kBallDark);
  gfx.fillTriangle(ax, ay, cx2, cy2, dx2, dy2, kBallDark);
}

void ppGachiDrawPlaceholder(Adafruit_GFX& gfx, int16_t x, int16_t y, int16_t boxW, int16_t boxH, uint8_t dex,
                            bool fill, const PpMenuColors& colors)
{
  int16_t side = static_cast<int16_t>(minI(boxW, boxH) - 4);
  if (side < 8) {
    return;
  }
  int16_t bx = static_cast<int16_t>(x + (boxW - side) / 2);
  int16_t by = static_cast<int16_t>(y + (boxH - side) / 2);
  if (fill) {
    gfx.fillRoundRect(bx, by, side, side, 8, colors.bg);
  }
  gfx.drawRoundRect(bx, by, side, side, 8, colors.dim);
  char num[4];
  putDex(num, dex);
  uint8_t size = side >= 80 ? 3 : 2;
  int16_t n = 0;
  while (num[n] != '\0') {
    ++n;
  }
  int16_t cw = static_cast<int16_t>(6 * size);
  if (n * cw > side - 4) {
    return;
  }
  gfx.setTextWrap(false);
  gfx.setTextSize(size);
  gfx.setTextColor(colors.dim);
  gfx.setCursor(static_cast<int16_t>(bx + (side - n * cw) / 2), static_cast<int16_t>(by + (side - 8 * size) / 2));
  gfx.print(num);
}

void ppGachiDrawBerry(Adafruit_GFX& gfx, int16_t cx, int16_t cy, int16_t r)
{
  if (r < 1) {
    return;
  }
  gfx.fillCircle(cx, cy, r, rgb(70, 60, 200));
  if (r >= 3) {
    gfx.fillCircle(cx - r / 3, cy - r / 3, maxI(1, r / 3), rgb(150, 150, 255));
  }
  int16_t lw = maxI(2, r * 2 / 3);
  int16_t lh = maxI(1, r / 3);
  gfx.fillEllipse(cx + lw / 2, cy - r - lh / 2, lw / 2 + 1, lh, rgb(60, 170, 60));
  gfx.drawFastVLine(cx, cy - r - lh, lh + 1, rgb(90, 70, 40));
}

void ppGachiStageDraw(Adafruit_GFX& canvas, int16_t sw, int16_t sh, const PpStagePose& pose,
                      uint8_t dex, bool shiny, uint8_t oldDex, const PpMenuColors& colors)
{
  if (sw <= 0 || sh <= 0) {
    return;
  }
  canvas.setTextWrap(false);
  SceneLook look = drawScene(canvas, sw, sh, pose.scene);
  int16_t groundY = groundOf(sh);

  if (pose.emptyBall) {
    int16_t r = minI(static_cast<int16_t>(sh * 28 / 100), static_cast<int16_t>(sw * 28 / 100));
    fillEllipseIn(canvas, sw, sh, sw / 2, groundY, r, maxI(2, r / 6), look.shadow);
    ppGachiDrawEmptyBall(canvas, sw / 2, groundY, r);
    return;
  }

  uint8_t drawDex = (pose.oldForm && oldDex != 0) ? oldDex : dex;
  const PpGachiSprite* sprite = ppGachiSpriteFor(drawDex, shiny);
  const PpGachiAnim* idle = ppGachiSpriteAnim(sprite, PP_SANIM_IDLE);
  const PpGachiAnim* anim = ppGachiSpriteAnim(sprite, pose.anim);
  uint8_t scale = petScale(sprite, sw, sh, groundY);

  PetSpot pet;
  pet.cx = static_cast<int16_t>(sw / 2 + pose.dx * sw / 240);
  pet.groundY = static_cast<int16_t>(groundY + pose.dy * sw / 240);

  if (idle == nullptr || anim == nullptr || scale == 0) {
    int16_t side = static_cast<int16_t>(sh * 60 / 100);
    ppGachiDrawPlaceholder(canvas, static_cast<int16_t>(pet.cx - side / 2), static_cast<int16_t>(pet.groundY - side),
                           side, side, drawDex, true, colors);
    pet.headY = static_cast<int16_t>(pet.groundY - side);
    pet.halfW = static_cast<int16_t>(side / 2);
    pet.frameTop = pet.headY;
    pet.tippedH = pet.halfW;
    pet.tippedW = side;
  } else {
    int16_t s = scale;
    int16_t top = 0;
    int16_t bottom = 0;
    frameRows(*idle, 0, top, bottom);
    pet.headY = static_cast<int16_t>(pet.groundY - (idle->baseline - top) * s);
    pet.halfW = static_cast<int16_t>(idle->w * s / 2);
    pet.tippedH = static_cast<int16_t>(idle->w * s);
    pet.tippedW = static_cast<int16_t>(minI(idle->h, idle->baseline) * s);

    uint8_t frame = static_cast<uint8_t>(pose.frame % anim->frames);
    int16_t frameGround = pet.groundY;
    if (!pose.tipped) {
      frameGround = static_cast<int16_t>(frameGround + keepInsideShift(*anim, frame, scale, groundY, sh));
    }
    frameRows(*anim, frame, top, bottom);
    pet.frameTop = pose.tipped ? static_cast<int16_t>(pet.groundY - pet.tippedH)
                               : static_cast<int16_t>(frameGround - (anim->baseline - top) * s);

    int16_t shadowW = pose.tipped ? static_cast<int16_t>(pet.tippedW / 2) : static_cast<int16_t>(pet.halfW * 8 / 10);
    fillEllipseIn(canvas, sw, sh, pet.cx, groundY, maxI(2, shadowW), maxI(1, s + s / 2), look.shadow);
    ppGachiDrawFrame(canvas, pet.cx, frameGround, scale, sprite, anim, frame, pose.mirror, pose.tipped);
  }
  drawSymbol(canvas, sw, sh, pose, pet, look.sky);
}

#endif  // PP_HAS_DISPLAY
