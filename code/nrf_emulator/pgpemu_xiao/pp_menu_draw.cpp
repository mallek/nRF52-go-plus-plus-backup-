#include "pp_menu_draw.h"

#if PP_HAS_DISPLAY

#include <string.h>

namespace {

// Text size 2 of the built-in font: 12 px per character, 16 px tall.
static constexpr uint8_t kTextSize = 2;
static constexpr int16_t kCharW = 6 * kTextSize;
static constexpr int16_t kCharH = 8 * kTextSize;

static constexpr int16_t kTitleY = 6;
static constexpr int16_t kRowsTop = 34;
static constexpr int16_t kRowH = 30;
static constexpr int16_t kHintsFromBottom = 18;
// Space kept under the rows for the "more below" marker and the hint line.
static constexpr int16_t kBottomReserve = 26;
static constexpr int16_t kSideMargin = 6;
static constexpr int16_t kTextInset = 8;
static constexpr int16_t kArrowW = 16;

// Prints at most maxChars characters of s, so a long text is cut, never wrapped.
void printCut(Adafruit_GFX& gfx, int16_t x, int16_t y, const char* s, int16_t maxChars, uint16_t color)
{
  if (maxChars <= 0) {
    return;
  }
  gfx.setTextSize(kTextSize);
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

// Return arrow in a 16 x 16 box: down the right side, then left to a head.
void drawBackArrow(Adafruit_GFX& gfx, int16_t x, int16_t y, uint16_t color)
{
  gfx.fillRect(x + 12, y + 2, 2, 9, color);
  gfx.fillRect(x + 6, y + 9, 8, 2, color);
  gfx.fillTriangle(x + 1, y + 10, x + 6, y + 5, x + 6, y + 15, color);
}

void drawRow(Adafruit_GFX& gfx, int16_t w, int16_t y, const PpMenuRowView& row, const PpMenuColors& colors)
{
  if (row.selected) {
    gfx.fillRect(kSideMargin, y, w - 2 * kSideMargin, kRowH - 4, colors.accent);
  }
  uint16_t ink = row.selected ? colors.bg : colors.fg;
  int16_t textY = y + (kRowH - 4 - kCharH) / 2;
  int16_t left = kSideMargin + kTextInset;
  int16_t right = w - kSideMargin - kTextInset;

  if (row.isBack) {
    drawBackArrow(gfx, left, textY, ink);
    left += kArrowW + 6;
  }

  // The value sits against the right edge; the label gets what is left.
  int16_t valueChars = cutLen(row.value, (right - left) / kCharW);
  int16_t valueX = right - valueChars * kCharW;
  int16_t labelRoom = valueChars > 0 ? valueX - kCharW - left : right - left;
  int16_t labelChars = cutLen(row.label, labelRoom / kCharW);
  printCut(gfx, left, textY, row.label, labelChars, ink);
  if (row.active && (labelChars + 2) * kCharW <= labelRoom) {
    printCut(gfx, left + labelChars * kCharW, textY, " *", 2, ink);
  }
  printCut(gfx, valueX, textY, row.value, valueChars, ink);
}

}  // namespace

uint8_t ppMenuRowsThatFit(int16_t h)
{
  int16_t rows = (h - kRowsTop - kBottomReserve) / kRowH;
  if (rows < 1) {
    rows = 1;
  }
  if (rows > PP_MENU_MAX_VISIBLE) {
    rows = PP_MENU_MAX_VISIBLE;
  }
  return static_cast<uint8_t>(rows);
}

void ppMenuDraw(Adafruit_GFX& gfx, int16_t w, int16_t h, const PpMenuView& view, const PpMenuColors& colors)
{
  gfx.fillScreen(colors.bg);
  gfx.setTextWrap(false);

  int16_t maxChars = (w - 2 * kSideMargin) / kCharW;
  int16_t titleChars = cutLen(view.title, maxChars);
  printCut(gfx, (w - titleChars * kCharW) / 2, kTitleY, view.title, titleChars, colors.dim);

  for (uint8_t i = 0; i < view.rowCount; ++i) {
    drawRow(gfx, w, kRowsTop + i * kRowH, view.rows[i], colors);
  }

  // Markers when the list continues above or below what is shown.
  int16_t mid = w / 2;
  if (view.firstRow > 0) {
    gfx.fillTriangle(mid - 6, kRowsTop - 3, mid + 6, kRowsTop - 3, mid, kRowsTop - 9, colors.dim);
  }
  if (view.firstRow + view.rowCount < view.totalRows) {
    int16_t y = kRowsTop + view.rowCount * kRowH - 2;
    gfx.fillTriangle(mid - 6, y, mid + 6, y, mid, y + 6, colors.dim);
  }

  // Each hint sits on the side of the button it names.
  // On a narrow screen both hints may not fit at the row text size; drop to the
  // small font before cutting anything.
  int16_t hintY = h - kHintsFromBottom;
  int16_t room = w - 2 * kSideMargin;
  int16_t bothChars = static_cast<int16_t>(strlen(view.hintLeft) + strlen(view.hintRight) + 1);
  uint8_t hintSize = (bothChars * kCharW <= room) ? kTextSize : 1;
  int16_t hintCharW = 6 * hintSize;
  int16_t half = (room / 2) / hintCharW;
  int16_t leftChars = cutLen(view.hintLeft, half);
  int16_t rightChars = cutLen(view.hintRight, half);
  gfx.setTextSize(hintSize);
  gfx.setTextColor(colors.dim);
  gfx.setCursor(kSideMargin, hintY);
  for (int16_t i = 0; i < leftChars; ++i) {
    gfx.write(static_cast<uint8_t>(view.hintLeft[i]));
  }
  gfx.setCursor(w - kSideMargin - rightChars * hintCharW, hintY);
  for (int16_t i = 0; i < rightChars; ++i) {
    gfx.write(static_cast<uint8_t>(view.hintRight[i]));
  }
}

#endif  // PP_HAS_DISPLAY
