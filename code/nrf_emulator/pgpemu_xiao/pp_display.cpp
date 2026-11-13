#include <Arduino.h>

#include "pp_board.h"
#include "pp_display.h"
#include "pp_identity.h"

#if PP_HAS_DISPLAY

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

namespace {

static constexpr uint32_t kComboHoldMs = 1500;
static constexpr uint32_t kMenuTimeoutMs = 20000;
static constexpr uint32_t kBacklightTimeoutMs = 60000;
static constexpr uint32_t kDebounceMs = 30;

static constexpr uint16_t kBg = ST77XX_BLACK;
static constexpr uint16_t kFg = ST77XX_WHITE;
static constexpr uint16_t kDim = 0x7BEF;     // grey
static constexpr uint16_t kAccent = 0xFD20;  // orange
static constexpr uint16_t kGood = ST77XX_GREEN;
static constexpr uint16_t kBad = ST77XX_RED;

static Adafruit_ST7789 tft(&SPI1, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

struct Button {
  uint8_t pin;
  bool down;
  bool rawLast;
  uint32_t changedAt;
  bool pressedEdge;
};

static Button buttonA = { PP_RESET_BUTTON_ARDUINO_PIN, false, false, 0, false };
static Button buttonB = { PP_SECOND_BUTTON_ARDUINO_PIN, false, false, 0, false };

enum Mode : uint8_t { MODE_STATUS, MODE_MENU };

static bool started = false;
static Mode mode = MODE_STATUS;
static bool comboArmed = false;
static uint32_t comboStartedAt = 0;
static bool waitForRelease = false;
static uint8_t menuIndex = 0;
static uint32_t menuActivityAt = 0;
static bool backlightOn = true;
static uint32_t backlightActivityAt = 0;

static bool connected = false;
static uint16_t caught = 0;
static uint16_t fled = 0;
static uint16_t spun = 0;
static const char* lastEvent = "";
static uint16_t lastEventColor = kDim;
static bool statusDirty = true;

static void readButton(Button& b, uint32_t now)
{
  bool raw = digitalRead(b.pin) == LOW;
  b.pressedEdge = false;
  if (raw != b.rawLast) {
    b.rawLast = raw;
    b.changedAt = now;
  }
  if (raw != b.down && (now - b.changedAt) >= kDebounceMs) {
    b.down = raw;
    b.pressedEdge = raw;
  }
}

static void setBacklight(bool on)
{
  if (on == backlightOn) {
    return;
  }
  backlightOn = on;
  digitalWrite(PIN_TFT_LITE, on ? HIGH : LOW);
}

static void touchBacklight(uint32_t now)
{
  backlightActivityAt = now;
  setBacklight(true);
}

static void centerText(const char* text, int16_t y, uint8_t size, uint16_t color)
{
  tft.setTextSize(size);
  tft.setTextColor(color, kBg);
  int16_t w = static_cast<int16_t>(strlen(text) * 6 * size);
  tft.setCursor((240 - w) / 2, y);
  tft.print(text);
}

static uint8_t nameSize(const char* name)
{
  size_t len = strlen(name);
  if (len <= 6) {
    return 6;
  }
  if (len <= 8) {
    return 4;
  }
  return 3;
}

static void drawStatusFrame()
{
  tft.fillScreen(kBg);
  char header[24];
  snprintf(header, sizeof(header), "PGP+ CLONE  %u/%u",
           static_cast<unsigned>(ppIdentityActive() + 1),
           static_cast<unsigned>(ppIdentityCount()));
  centerText(header, 6, 2, kDim);
  const char* name = ppIdentityName(ppIdentityActive());
  uint8_t size = nameSize(name);
  centerText(name, 70 - (size * 8) / 2, size, kAccent);
  centerText(ppIdentityCount() > 1 ? "A+B: switch  hold A: reset" : "hold A 5s: reset", 226, 1, kDim);
  statusDirty = true;
}

static void drawStatusLines()
{
  tft.fillRect(0, 120, 240, 100, kBg);
  centerText(connected ? "CONNECTED" : "WAITING", 124, 3, connected ? kGood : kFg);
  char counts[32];
  snprintf(counts, sizeof(counts), "caught %u  fled %u", caught, fled);
  centerText(counts, 164, 2, kFg);
  char spins[16];
  snprintf(spins, sizeof(spins), "spun %u", spun);
  centerText(spins, 184, 2, kFg);
  centerText(lastEvent, 206, 2, lastEventColor);
  statusDirty = false;
}

static void drawMenu()
{
  tft.fillScreen(kBg);
  centerText("SWITCH IDENTITY", 6, 2, kDim);
  uint8_t count = ppIdentityCount();
  int16_t rowH = 32;
  int16_t top = 40;
  for (uint8_t i = 0; i < count && i < 5; ++i) {
    int16_t y = top + i * rowH;
    bool selected = (i == menuIndex);
    if (selected) {
      tft.fillRect(10, y - 4, 220, rowH - 2, kAccent);
    }
    tft.setTextSize(3);
    tft.setTextColor(selected ? kBg : kFg, selected ? kAccent : kBg);
    tft.setCursor(20, y);
    tft.print(ppIdentityName(i));
    if (i == ppIdentityActive()) {
      tft.print(" *");
    }
  }
  centerText("B: next   A: select", 214, 2, kDim);
}

static void openMenu(uint32_t now)
{
  mode = MODE_MENU;
  menuIndex = ppIdentityActive();
  menuActivityAt = now;
  waitForRelease = true;
  touchBacklight(now);
  Serial.println("Display: identity menu open");
  drawMenu();
}

static void closeMenu()
{
  mode = MODE_STATUS;
  waitForRelease = true;
  drawStatusFrame();
}

static void updateMenu(uint32_t now)
{
  if (now - menuActivityAt >= kMenuTimeoutMs) {
    Serial.println("Display: menu timeout");
    closeMenu();
    return;
  }
  if (buttonB.pressedEdge) {
    menuIndex = static_cast<uint8_t>((menuIndex + 1) % ppIdentityCount());
    menuActivityAt = now;
    drawMenu();
    return;
  }
  if (buttonA.pressedEdge) {
    if (menuIndex == ppIdentityActive()) {
      closeMenu();
      return;
    }
    tft.fillScreen(kBg);
    centerText("SWITCHING TO", 80, 2, kDim);
    centerText(ppIdentityName(menuIndex), 110, 4, kAccent);
    centerText("then pair it in GO", 160, 2, kFg);
    delay(800);
    ppIdentitySwitchTo(menuIndex);
  }
}

static void updateCombo(uint32_t now)
{
  if (ppIdentityCount() < 2) {
    return;
  }
  bool both = buttonA.down && buttonB.down;
  if (!both) {
    comboArmed = false;
    return;
  }
  if (!comboArmed) {
    comboArmed = true;
    comboStartedAt = now;
    return;
  }
  if (now - comboStartedAt >= kComboHoldMs) {
    comboArmed = false;
    openMenu(now);
  }
}

}  // namespace

void ppDisplayBegin()
{
  pinMode(buttonA.pin, INPUT_PULLUP);
  pinMode(buttonB.pin, INPUT_PULLUP);
  pinMode(PIN_TFT_LITE, OUTPUT);
  digitalWrite(PIN_TFT_LITE, HIGH);
  tft.init(240, 240);
  tft.setRotation(1);
  tft.setTextWrap(false);
  started = true;
  backlightActivityAt = millis();
  drawStatusFrame();
  drawStatusLines();
  Serial.println("Display: ST7789 up");
}

void ppDisplayUpdate()
{
  if (!started) {
    return;
  }
  uint32_t now = millis();
  readButton(buttonA, now);
  readButton(buttonB, now);

  if (buttonA.pressedEdge || buttonB.pressedEdge) {
    bool wasDark = !backlightOn;
    touchBacklight(now);
    if (wasDark) {
      // First press only wakes the screen.
      buttonA.pressedEdge = false;
      buttonB.pressedEdge = false;
    }
  }

  if (waitForRelease) {
    if (!buttonA.down && !buttonB.down) {
      waitForRelease = false;
    }
    buttonA.pressedEdge = false;
    buttonB.pressedEdge = false;
  }

  if (mode == MODE_MENU) {
    updateMenu(now);
    return;
  }

  if (!waitForRelease) {
    updateCombo(now);
  }
  if (mode == MODE_STATUS && statusDirty) {
    drawStatusLines();
  }
  if (backlightOn && now - backlightActivityAt >= kBacklightTimeoutMs) {
    setBacklight(false);
  }
}

void ppDisplayOnEvent(PpFeedbackEvent event)
{
  switch (event) {
    case PP_FEEDBACK_CONNECTED: {
      connected = true;
      lastEvent = "";
      break;
    }
    case PP_FEEDBACK_DISCONNECTED: {
      connected = false;
      lastEvent = "disconnected";
      lastEventColor = kBad;
      break;
    }
    case PP_FEEDBACK_CAUGHT: {
      ++caught;
      lastEvent = "caught!";
      lastEventColor = kGood;
      break;
    }
    case PP_FEEDBACK_FLED: {
      ++fled;
      lastEvent = "fled";
      lastEventColor = kBad;
      break;
    }
    case PP_FEEDBACK_POKESTOP_SPUN: {
      ++spun;
      lastEvent = "spun";
      lastEventColor = ST77XX_CYAN;
      break;
    }
    case PP_FEEDBACK_POKEMON_FULL: {
      lastEvent = "POKEMON FULL";
      lastEventColor = kAccent;
      break;
    }
    case PP_FEEDBACK_BAG_FULL: {
      lastEvent = "BAG FULL";
      lastEventColor = ST77XX_CYAN;
      break;
    }
    default: {
      return;
    }
  }
  statusDirty = true;
}

void ppDisplayOnFactoryReset()
{
  if (!started) {
    return;
  }
  connected = false;
  caught = 0;
  fled = 0;
  spun = 0;
  lastEvent = "RESET - pair in GO";
  lastEventColor = kAccent;
  statusDirty = true;
  touchBacklight(millis());
}

void ppDisplayShutdown()
{
  if (!started) {
    return;
  }
  tft.fillScreen(kBg);
  digitalWrite(PIN_TFT_LITE, LOW);
  tft.enableSleep(true);
}

bool ppUiOwnsButtons()
{
  if (!started) {
    return false;
  }
  return mode == MODE_MENU || buttonB.down || comboArmed || waitForRelease;
}

#else

void ppDisplayBegin() {}
void ppDisplayUpdate() {}
void ppDisplayOnEvent(PpFeedbackEvent) {}
void ppDisplayOnFactoryReset() {}
void ppDisplayShutdown() {}
bool ppUiOwnsButtons()
{
  return false;
}

#endif
