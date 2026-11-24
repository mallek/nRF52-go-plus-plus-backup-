#include <Arduino.h>

#include "pp_board.h"
#include "pp_anim.h"
#include "pp_anim_logic.h"
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
// After a catch or spin animation the status screen stays lit this long.
static constexpr uint32_t kAfterAnimHoldMs = 5000;
static constexpr uint32_t kAnimFrameMs = 50;
static constexpr uint32_t kDebounceMs = 30;
// After a disconnect the screen shows the state this long, then goes dark; a
// button tap while disconnected shows it again for the same time.
static constexpr uint32_t kDisconnectedShowMs = 5000;
// Keeps the screen lit while a button is held (reset hold, A+B combo).
static constexpr uint32_t kHeldKeepMs = 2000;
// Must match PHYSICAL_RESET_BUTTON_HOLD_MS in the sketch; only drives the bar.
static constexpr uint32_t kResetHoldMs = 5000;
static constexpr uint32_t kResetHoldShowAfterMs = 700;
static constexpr uint32_t kResetHoldDrawMs = 100;

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
static uint32_t backlightUntil = 0;

static bool connected = false;
// False until the first connect after boot or factory reset: WAITING vs DISCONNECTED.
static bool everConnected = false;
static bool sleepArmed = false;
static uint32_t sleepAt = 0;
static uint32_t countdownShown = 0xffffffff;

static bool holdActive = false;
static bool holdShown = false;
static bool holdFired = false;
static uint32_t holdStartedAt = 0;
static uint32_t holdDrawAt = 0;
static uint16_t caught = 0;
static uint16_t fled = 0;
static uint16_t spun = 0;
static const char* lastEvent = "";
static uint16_t lastEventColor = kDim;
static bool statusDirty = true;

static PpAnimState anim = { PP_ANIM_NONE, 0 };
static uint32_t animFrameAt = 0;
static PpTapCounter eggTaps = { 0, 0 };

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

static void touchBacklight(uint32_t now, uint32_t forMs = kBacklightTimeoutMs)
{
  uint32_t until = now + forMs;
  if (!backlightOn || static_cast<int32_t>(until - backlightUntil) > 0) {
    backlightUntil = until;
  }
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

static void leftText(const char* text, int16_t y, uint8_t size, uint16_t color)
{
  tft.setTextSize(size);
  tft.setTextColor(color, kBg);
  tft.setCursor(4, y);
  tft.print(text);
}

static void rightText(const char* text, int16_t y, uint8_t size, uint16_t color)
{
  tft.setTextSize(size);
  tft.setTextColor(color, kBg);
  tft.setCursor(240 - 4 - static_cast<int16_t>(strlen(text) * 6 * size), y);
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
  // Each hint sits on the side of the button it names: A left, B right.
  leftText("A hold: reset", 226, 1, kDim);
  if (ppIdentityCount() > 1) {
    centerText("A+B: menu", 226, 1, kDim);
  }
  rightText("B: wake", 226, 1, kDim);
  statusDirty = true;
}

static bool countdownVisible()
{
  return !connected && sleepArmed;
}

static uint32_t sleepSecondsLeft(uint32_t now)
{
  int32_t leftMs = static_cast<int32_t>(sleepAt - now);
  return leftMs > 0 ? static_cast<uint32_t>(leftMs + 999) / 1000 : 0;
}

// "sleep in MM:SS" under the state word. Fixed width, so it overwrites itself.
static void drawCountdown(uint32_t now)
{
  uint32_t secs = sleepSecondsLeft(now);
  char line[20];
  snprintf(line, sizeof(line), "sleep in %02u:%02u",
           static_cast<unsigned>(secs / 60), static_cast<unsigned>(secs % 60));
  centerText(line, 150, 2, kAccent);
  countdownShown = secs;
}

static void drawStatusLines()
{
  tft.fillRect(0, 120, 240, 104, kBg);
  // With the countdown showing, the counters move down a little.
  bool countdown = countdownVisible();
  int16_t shift = countdown ? 6 : 0;
  if (connected) {
    centerText("CONNECTED", 124, 3, kGood);
  } else if (everConnected) {
    centerText("DISCONNECTED", 122, 3, kBad);
  } else {
    centerText("WAITING", 122, 3, kFg);
  }
  if (countdown) {
    drawCountdown(millis());
  }
  char counts[32];
  snprintf(counts, sizeof(counts), "caught %u  fled %u", caught, fled);
  centerText(counts, 164 + shift, 2, kFg);
  char spins[16];
  snprintf(spins, sizeof(spins), "spun %u", spun);
  centerText(spins, 182 + shift, 2, kFg);
  centerText(lastEvent, 206, 2, lastEventColor);
  statusDirty = false;
}

// Bar that fills while A is held toward the factory reset.
static void drawResetHold(uint32_t heldMs, bool first)
{
  if (first) {
    tft.fillRect(0, 120, 240, 104, kBg);
    centerText("HOLD TO RESET", 128, 2, kAccent);
    tft.drawRect(20, 156, 200, 26, kFg);
    centerText("release to cancel", 194, 1, kDim);
  }
  uint32_t clamped = heldMs > kResetHoldMs ? kResetHoldMs : heldMs;
  int16_t w = static_cast<int16_t>((clamped * 196u) / kResetHoldMs);
  tft.fillRect(22, 158, w, 22, kBad);
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
  // Each hint sits on the side of the button it names: A left, B right.
  leftText("A:select", 214, 2, kDim);
  rightText("B:next", 214, 2, kDim);
}

static void startAnim(PpAnimKind kind, uint32_t now)
{
  ppAnimStart(anim, kind, now);
  animFrameAt = now - kAnimFrameMs;
  touchBacklight(now, ppAnimDurationMs(kind) + kAfterAnimHoldMs);
}

static void openMenu(uint32_t now)
{
  anim.kind = PP_ANIM_NONE;
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

// Shows the fill bar while A alone is held. The reset itself is done by the
// lifecycle code, which then calls ppDisplayOnFactoryReset().
static void updateResetHold(uint32_t now)
{
  if (!buttonA.down) {
    holdFired = false;
  }
  bool holding = mode == MODE_STATUS && buttonA.down && !buttonB.down && !waitForRelease && !comboArmed;
  if (!holding || holdFired) {
    if (holdShown) {
      holdShown = false;
      statusDirty = true;
    }
    holdActive = false;
    return;
  }
  if (!holdActive) {
    holdActive = true;
    holdStartedAt = now;
    return;
  }
  uint32_t held = now - holdStartedAt;
  if (anim.kind != PP_ANIM_NONE) {
    // An animation owns the screen; the bar is redrawn in full after it.
    holdShown = false;
    return;
  }
  if (held < kResetHoldShowAfterMs) {
    return;
  }
  if (!holdShown) {
    drawResetHold(held, true);
    holdShown = true;
    holdDrawAt = now;
  } else if (now - holdDrawAt >= kResetHoldDrawMs) {
    drawResetHold(held, false);
    holdDrawAt = now;
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
  backlightUntil = millis() + kBacklightTimeoutMs;
  drawStatusFrame();
  drawStatusLines();
  startAnim(PP_ANIM_BOOT, millis());
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
    bool brief = mode == MODE_STATUS && !connected && everConnected;
    touchBacklight(now, brief ? kDisconnectedShowMs : kBacklightTimeoutMs);
    if (wasDark) {
      // First press only wakes the screen.
      buttonA.pressedEdge = false;
      buttonB.pressedEdge = false;
    }
  }

  if (buttonA.down || buttonB.down) {
    touchBacklight(now, kHeldKeepMs);
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
  updateResetHold(now);
  // Easter egg: five quick B taps on the status screen.
  if (mode == MODE_STATUS && buttonB.pressedEdge && !buttonA.down && ppTapCounterTap(eggTaps, now)) {
    Serial.println("Display: easter egg");
    startAnim(PP_ANIM_EGG, now);
  }
  if (anim.kind != PP_ANIM_NONE && mode == MODE_STATUS) {
    if (ppAnimDone(anim, now)) {
      anim.kind = PP_ANIM_NONE;
      drawStatusFrame();
      drawStatusLines();
    } else if (now - animFrameAt >= kAnimFrameMs) {
      animFrameAt = now;
      if (!ppAnimDrawFrame(tft, anim.kind, ppAnimProgress(anim, now))) {
        anim.kind = PP_ANIM_NONE;
        drawStatusFrame();
      }
    }
  } else if (mode == MODE_STATUS && !holdShown) {
    if (statusDirty) {
      drawStatusLines();
    } else if (backlightOn && countdownVisible() && sleepSecondsLeft(now) != countdownShown) {
      drawCountdown(now);
    }
  }
  if (backlightOn && static_cast<int32_t>(now - backlightUntil) >= 0) {
    setBacklight(false);
  }
}

void ppDisplayOnEvent(PpFeedbackEvent event)
{
  switch (event) {
    case PP_FEEDBACK_CONNECTED: {
      connected = true;
      everConnected = true;
      lastEvent = "";
      break;
    }
    case PP_FEEDBACK_DISCONNECTED: {
      connected = false;
      if (everConnected) {
        // The state word says it; show the screen briefly, then go dark.
        lastEvent = "";
        if (started && mode == MODE_STATUS) {
          touchBacklight(millis(), kDisconnectedShowMs);
        }
      }
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
    case PP_FEEDBACK_READY: {
      connected = true;
      everConnected = true;
      lastEvent = "ready!";
      lastEventColor = ST77XX_YELLOW;
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
  PpAnimKind kind = ppAnimKindForEvent(event);
  if (started && mode == MODE_STATUS && kind != PP_ANIM_NONE) {
    startAnim(kind, millis());
  }
}

void ppDisplayOnFactoryReset()
{
  if (!started) {
    return;
  }
  connected = false;
  everConnected = false;
  caught = 0;
  fled = 0;
  spun = 0;
  lastEvent = "RESET - pair in GO";
  lastEventColor = kAccent;
  statusDirty = true;
  holdFired = true;
  holdShown = false;
  holdActive = false;
  uint32_t now = millis();
  touchBacklight(now);
  if (mode == MODE_STATUS) {
    startAnim(PP_ANIM_RESET, now);
  }
}

void ppDisplaySetSleepDeadline(bool armed, uint32_t atMs)
{
  if (armed != sleepArmed) {
    statusDirty = true;
  }
  sleepArmed = armed;
  sleepAt = atMs;
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
void ppDisplaySetSleepDeadline(bool, uint32_t) {}
void ppDisplayShutdown() {}
bool ppUiOwnsButtons()
{
  return false;
}

#endif
