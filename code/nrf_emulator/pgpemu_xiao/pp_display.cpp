#include <Arduino.h>

#include "pp_board.h"
#include "pp_anim.h"
#include "pp_anim_logic.h"
#include "pp_display.h"
#include "pp_gachi.h"
#include "pp_gachi_flow.h"
#include "pp_gachi_species.h"
#include "pp_gachi_sprites.h"
#include "pp_gachi_stage.h"
#include "pp_gachi_stage_draw.h"
#include "pp_gachi_text.h"
#include "pp_gachi_ui.h"
#include "pp_identity.h"
#include "pp_menu_draw.h"
#include "pp_menu_items.h"
#include "pp_menu_logic.h"
#include "pp_settings.h"

#if PP_HAS_DISPLAY

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// Battery: the screen stays dark on catch and spin and the panel controller
// sleeps while the backlight is off. Both ideas are from Jack's Plus+ emulator,
// https://github.com/Jackv-makes/nRF52-PoGoPP-Emu (AGPL, same kiyoukan fork).

namespace {

// How long the screen stays lit after a press: the Screen sleep setting.
static uint32_t screenSleepMs()
{
  return ppSettingsScreenSleepMs(ppSettings());
}

// While a PokeGachi card or battle is on screen the player is reading or
// thinking, so the screen stays lit at least this long whatever the Screen
// sleep setting says. A short setting is for glances at the home screen.
static constexpr uint32_t kPlayMinSleepMs = 30000;

static uint32_t playSleepMs()
{
  uint32_t setting = screenSleepMs();
  return setting > kPlayMinSleepMs ? setting : kPlayMinSleepMs;
}

static constexpr uint32_t kComboHoldMs = 1500;
// After an animation the status screen stays lit this long.
static constexpr uint32_t kAfterAnimHoldMs = 5000;
static constexpr uint32_t kAnimFrameMs = 50;
static constexpr uint32_t kDebounceMs = 30;
// After a disconnect the screen shows the state this long, then goes dark; a
// button tap while disconnected shows it again for the same time.
static constexpr uint32_t kDisconnectedShowMs = 5000;
// Keeps the screen lit while a button is held (reset hold, A+B combo).
static constexpr uint32_t kHeldKeepMs = 2000;
// A warning (box full, no balls) on a dark screen lights it this long.
static constexpr uint32_t kWarningShowMs = 8000;
// The ST7789 ignores Sleep Out sent sooner than this after Sleep In.
static constexpr uint32_t kPanelSleepGapMs = 120;
// Wait after Sleep Out before the next command.
static constexpr uint32_t kPanelWakeSettleMs = 5;
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
static const PpMenuColors kColors = { kBg, kFg, kDim, kAccent };

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

// MODE_GACHI: a PokeGachi result card or battle owns the screen and the buttons.
// MODE_MOMENT: the play walk or the evolve flicker on the home layout, before
// its result card; it owns the buttons too, and any press skips it.
enum Mode : uint8_t { MODE_STATUS, MODE_MENU, MODE_GACHI, MODE_MOMENT };

static bool started = false;
static Mode mode = MODE_STATUS;
static bool comboArmed = false;
static uint32_t comboStartedAt = 0;
static bool waitForRelease = false;
static PpMenuState menuState = {};
static bool backlightOn = true;
static uint32_t backlightUntil = 0;
static uint32_t panelSleptAt = 0;

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
// lastEvent is a warning worth reading (POKEMON FULL, BAG FULL, NO BALLS, the
// reset text). Only those take the counters' place on the pet face.
static bool lastEventWarns = false;
static bool statusDirty = true;

static PpAnimState anim = { PP_ANIM_NONE, 0 };
static uint32_t animFrameAt = 0;
// Five quick B taps on the home screen open the PokeGachi page.
static PpTapCounter quickTaps = { 0, 0 };

// The card or battle shown in MODE_GACHI, and the mode it returns to.
static PpGachiFlow flow = {};
static Mode flowReturn = MODE_STATUS;
// A card keeps its line pointers until it closes, so a composed line (the
// shiny arrival) lives here, not on the stack.
static char cardLine[32];
// The card's sprite animation (Animations on): when the card showed, the frame
// on screen and when the frame was last checked.
static uint32_t cardShownAt = 0;
static uint8_t cardFrameShown = 0;
static uint32_t cardPosedAt = 0;
// The result card a play or evolve moment shows when it ends. The game result
// it reports was applied and saved before the moment began.
static PpGachiCard momentCard = {};

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

static void redrawAfterWake();

// Dark means backlight off AND the panel controller asleep. Nothing is drawn
// while dark; waking repaints the screen before the light comes on.
static void setBacklight(bool on)
{
  if (on == backlightOn) {
    return;
  }
  backlightOn = on;
  if (on) {
    uint32_t since = millis() - panelSleptAt;
    if (since < kPanelSleepGapMs) {
      delay(kPanelSleepGapMs - since);
    }
    tft.enableSleep(false);
    delay(kPanelWakeSettleMs);
    redrawAfterWake();
    digitalWrite(PIN_TFT_LITE, HIGH);
  } else {
    digitalWrite(PIN_TFT_LITE, LOW);
    tft.enableSleep(true);
    panelSleptAt = millis();
  }
}

static void touchBacklight(uint32_t now, uint32_t forMs = screenSleepMs())
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

static bool countdownVisible()
{
  return !connected && sleepArmed;
}

static uint32_t sleepSecondsLeft(uint32_t now)
{
  int32_t leftMs = static_cast<int32_t>(sleepAt - now);
  return leftMs > 0 ? static_cast<uint32_t>(leftMs + 999) / 1000 : 0;
}

// The pet face's link word and its color: the same rules as the text screen.
// One place returns each text, so the pointer tells which word is meant.
static const char* gachiLinkWord(uint16_t& color)
{
  if (connected) {
    color = kGood;
    return "CONNECTED";
  }
  if (everConnected) {
    color = kBad;
    return "DISCONNECTED";
  }
  color = kFg;
  return "WAITING";
}

// ---- The pet stage (PokeGachi on) -----------------------------------------
//
// The upper part of the pet face: a base state decided by the catcher, with at
// most one reaction on top. Posed only while the home screen is lit and
// Animations is on; with Animations off it is drawn once per home screen draw.

static PpStage stage;
static bool stageInited = false;
// The current sprite's frame timings, and whose they are.
static PpStageTimings stageTimings = {};
static bool timingsLoaded = false;
static bool timingsHasPet = false;
static uint8_t timingsDex = 0;
static bool timingsShiny = false;
// GO finished its handshake (READY) since the last disconnect: the pet is awake.
// The raw connect does not wake it.
static bool goReady = false;
// When the stage was last posed. The tick asks at most every kStageFrameMs, so
// the stage is redrawn at most about ten times a second.
static uint32_t stagePosedAt = 0;
static constexpr uint32_t kStageFrameMs = 100;
// The form the pet is leaving, drawn during the evolve flicker; 0 otherwise.
static uint8_t stageOldDex = 0;

static PpStageBase stageBaseWanted()
{
  return ppStageBaseFor(ppGachiState().hasPet, goReady);
}

// Brings the stage in line with the pet, the Animations setting and the
// catcher. Cheap: on every event, every home screen draw and every tick,
// whether or not the stage is on screen.
static void syncStage(uint32_t now)
{
  if (!stageInited) {
    ppStageInit(stage, now);
    stageInited = true;
  }
  const PpGachiState& st = ppGachiState();
  bool petChanged = st.hasPet != timingsHasPet || (st.hasPet && (st.petDex != timingsDex || st.petShiny != timingsShiny));
  if (!timingsLoaded || petChanged) {
    ppGachiSpriteTimings(st.hasPet ? ppGachiSpriteFor(st.petDex, st.petShiny) : nullptr, stageTimings);
    timingsLoaded = true;
    timingsHasPet = st.hasPet;
    timingsDex = st.petDex;
    timingsShiny = st.petShiny;
  }
  bool animations = ppSettings().animations;
  if (stage.animationsOn != animations) {
    ppStageSetAnimations(stage, animations);
  }
  ppStageSetBase(stage, stageBaseWanted(), now);
}

// True while the stage is what the screen shows: the lit home screen with the
// pet face and no old-style animation over it.
static bool stageOnScreen()
{
  return started && mode == MODE_STATUS && backlightOn && anim.kind == PP_ANIM_NONE && ppGachiEnabled();
}

// Draws the stage at `pose` into the shared canvas and pushes it to the stage
// rectangle. Without a canvas: the still pet (or the empty ball) on a plain
// background, and false.
static bool drawStage(const PpStagePose& pose)
{
  int16_t x = 0;
  int16_t y = 0;
  int16_t sw = 0;
  int16_t sh = 0;
  ppGachiStageRect(tft.width(), tft.height(), x, y, sw, sh);
  if (sw <= 0 || sh <= 0) {
    return false;
  }
  const PpGachiState& st = ppGachiState();
  GFXcanvas16* canvas = ppAnimCanvas();
  if (canvas != nullptr) {
    ppGachiStageDraw(*canvas, sw, sh, pose, st.petDex, st.petShiny, stageOldDex, kColors);
    if (ppGachiStagePush(tft, *canvas, x, y, sw, sh)) {
      return true;
    }
  }
  tft.fillRect(x, y, sw, sh, kBg);
  if (st.hasPet) {
    ppGachiDrawSprite(tft, x, y, sw, sh, st.petDex, st.petShiny, kColors);
  } else {
    int16_t r = static_cast<int16_t>((sw < sh ? sw : sh) * 28 / 100);
    ppGachiDrawEmptyBall(tft, static_cast<int16_t>(x + sw / 2), static_cast<int16_t>(y + sh - sh * 18 / 100), r);
  }
  return false;
}

// One stage step from ppDisplayUpdate, while the stage is on screen and
// Animations is on: redraws only when the pose changed.
static void tickStage(uint32_t now)
{
  if (static_cast<uint32_t>(now - stagePosedAt) < kStageFrameMs) {
    return;
  }
  if (ppAnimCanvas() == nullptr) {
    // No canvas: the still drawn with the home screen stays.
    return;
  }
  stagePosedAt = now;
  syncStage(now);
  if (stage.reaction != PP_REACT_NONE && !ppStageBusy(stage, now)) {
    // The reaction is over: forget it, so a millis() wrap can never bring it back.
    ppStageCancel(stage);
  }
  PpStagePose pose;
  if (ppStagePose(stage, stageTimings, now, pose)) {
    drawStage(pose);
  }
}

// What the last full pet face drew above the lines band: the link word on the
// top line, the pet (or the empty ball) and the stage's base state.
static const char* frameLink = nullptr;
static bool frameHasPet = false;
static uint8_t frameDex = 0;
static bool frameShiny = false;
static PpStageBase frameBase = PP_STAGE_EMPTY;

// True when the top line or the stage on screen no longer matches the state,
// so a lines-only redraw would leave them stale.
static bool gachiFrameStale()
{
  uint16_t color = 0;
  const PpGachiState& st = ppGachiState();
  if (gachiLinkWord(color) != frameLink || st.hasPet != frameHasPet || stageBaseWanted() != frameBase) {
    return true;
  }
  return st.hasPet && (st.petDex != frameDex || st.petShiny != frameShiny);
}

// The pet face's text lines, built from the same state the text screen shows.
// The buffers are static so the pointers outlive this call; the draw that
// uses them follows at once. Records the countdown second it shows.
static PpGachiHomeLines gachiHomeLines(uint32_t now)
{
  static char top[24];
  static char countdown[20];
  static char counts[40];
  PpGachiHomeLines lines;
  snprintf(top, sizeof(top), "%s %u/%u", ppIdentityName(ppIdentityActive()),
           static_cast<unsigned>(ppIdentityActive() + 1),
           static_cast<unsigned>(ppIdentityCount()));
  lines.topLeft = top;
  lines.topRight = gachiLinkWord(lines.topRightColor);
  lines.pantryOverride = nullptr;
  if (countdownVisible()) {
    uint32_t secs = sleepSecondsLeft(now);
    snprintf(countdown, sizeof(countdown), "sleep in %02u:%02u",
             static_cast<unsigned>(secs / 60), static_cast<unsigned>(secs % 60));
    lines.pantryOverride = countdown;
    countdownShown = secs;
  }
  if (lastEventWarns && lastEvent[0] != '\0') {
    lines.bottom = lastEvent;
    lines.bottomColor = lastEventColor;
  } else {
    snprintf(counts, sizeof(counts), "caught %u  fled %u  spun %u", caught, fled, spun);
    lines.bottom = counts;
    lines.bottomColor = kFg;
  }
  return lines;
}

// With PokeGachi on the home screen is the pet face, which draws its text lines
// too; otherwise the text screen frame, and the lines follow on statusDirty.
static void drawStatusFrame()
{
  if (ppGachiEnabled()) {
    uint32_t now = millis();
    PpGachiHomeLines lines = gachiHomeLines(now);
    const PpGachiState& st = ppGachiState();
    ppGachiDrawHome(tft, tft.width(), tft.height(), st, lines, kColors);
    // The stage at its pose now. With Animations off this is the only time it
    // is drawn; with them on the tick takes over from this pose.
    syncStage(now);
    PpStagePose pose;
    ppStagePose(stage, stageTimings, now, pose);
    drawStage(pose);
    stagePosedAt = now;
    frameLink = lines.topRight;
    frameHasPet = st.hasPet;
    frameDex = st.petDex;
    frameShiny = st.petShiny;
    frameBase = stage.base;
    statusDirty = false;
    return;
  }
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
  centerText("A+B: menu", 226, 1, kDim);
  rightText("B: wake", 226, 1, kDim);
  statusDirty = true;
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
  if (ppGachiEnabled()) {
    if (gachiFrameStale()) {
      // A connect, a disconnect or a new pet changed what sits above the band.
      drawStatusFrame();
      return;
    }
    PpGachiHomeLines lines = gachiHomeLines(millis());
    ppGachiDrawHomeLines(tft, tft.width(), tft.height(), ppGachiState(), lines, kColors);
    statusDirty = false;
    return;
  }
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

// The whole home screen. The pet face's frame already drew its lines.
static void drawHomeScreen()
{
  drawStatusFrame();
  if (statusDirty) {
    drawStatusLines();
  }
}

// Bar that fills while A is held toward the factory reset. It covers the status
// lines, which on the pet face is the band under the sprite.
static void drawResetHold(uint32_t heldMs, bool first)
{
  int16_t top = 120;
  int16_t bandH = 104;
  if (ppGachiEnabled()) {
    top = ppGachiHomeLinesTop(tft.height());
    bandH = static_cast<int16_t>(tft.height() - top);
  }
  if (first) {
    tft.fillRect(0, top, 240, bandH, kBg);
    centerText("HOLD TO RESET", top + 8, 2, kAccent);
    tft.drawRect(20, top + 36, 200, 26, kFg);
    centerText("release to cancel", top + 74, 1, kDim);
  }
  uint32_t clamped = heldMs > kResetHoldMs ? kResetHoldMs : heldMs;
  int16_t w = static_cast<int16_t>((clamped * 196u) / kResetHoldMs);
  tft.fillRect(22, top + 38, w, 22, kBad);
}

static void drawMenu()
{
  PpMenuView view;
  ppMenuBuildView(menuState, ppSettingsMenu(), ppMenuRowsThatFit(tft.height()), view);
  ppMenuDraw(tft, tft.width(), tft.height(), view, kColors);
}

static void drawFlow()
{
  if (flow.screen == PP_GSCREEN_CARD) {
    // The whole card, with frame 0 of its sprite's animation.
    ppGachiDrawCard(tft, tft.width(), tft.height(), flow.card, kColors);
    cardFrameShown = 0;
  } else if (flow.screen == PP_GSCREEN_BATTLE) {
    const PpGachiState& st = ppGachiState();
    ppGachiDrawBattle(tft, tft.width(), tft.height(), flow.battle, flow.roundShowing, st.petDex, st.petShiny, kColors);
  }
}

// One step of a card's sprite animation, while a card shows on a lit screen
// with Animations on: redraws only the sprite area, only when the frame
// changed, at most every kStageFrameMs. The battle screen never moves.
static void tickCard(uint32_t now)
{
  if (flow.screen != PP_GSCREEN_CARD || flow.card.dex == 0 || !backlightOn || !ppSettings().animations) {
    return;
  }
  if (static_cast<uint32_t>(now - cardPosedAt) < kStageFrameMs) {
    return;
  }
  GFXcanvas16* canvas = ppAnimCanvas();
  if (canvas == nullptr) {
    // No canvas: the card keeps the frame it was drawn with.
    return;
  }
  cardPosedAt = now;
  PpStageAnim want = flow.card.spriteAnim < PP_SANIM_COUNT ? static_cast<PpStageAnim>(flow.card.spriteAnim) : PP_SANIM_IDLE;
  const PpGachiAnim* a = ppGachiSpriteAnim(ppGachiSpriteFor(flow.card.dex, flow.card.shiny), want);
  if (a == nullptr) {
    // The placeholder: nothing moves.
    return;
  }
  // A card shown later in this loop than `now` was read counts from 0.
  uint32_t elapsed = static_cast<int32_t>(now - cardShownAt) > 0 ? now - cardShownAt : 0;
  PpAnimTiming timing = { a->frames, a->ms };
  uint8_t frame = ppStageLoopFrame(timing, elapsed);
  if (frame == cardFrameShown) {
    return;
  }
  cardFrameShown = frame;
  // The card's sprite area lies in the upper band the stage also uses, and the
  // card is plain background there apart from it; its text starts below the
  // band. So the band is drawn whole in the canvas and pushed in one go.
  int16_t w = tft.width();
  int16_t band = ppGachiHomeLinesTop(tft.height());
  canvas->fillRect(0, 0, w, band, kBg);
  ppGachiDrawCardSprite(*canvas, w, tft.height(), flow.card, frame, kColors);
  ppGachiStagePush(tft, *canvas, 0, 0, w, band);
}

static void redrawAfterWake()
{
  if (mode == MODE_MENU) {
    drawMenu();
    return;
  }
  if (mode == MODE_GACHI) {
    drawFlow();
    return;
  }
  if (mode == MODE_MOMENT) {
    // Not reached in practice (sleeping ends a moment), but never a blank screen.
    drawStatusFrame();
    return;
  }
  if (anim.kind != PP_ANIM_NONE) {
    // The animation draws its own frames and the status screen follows it.
    return;
  }
  // The screen has just been lit: an idle pet sets off on a walk.
  ppStageNudge(stage, millis());
  drawHomeScreen();
}

static void startAnim(PpAnimKind kind, uint32_t now)
{
  ppAnimStart(anim, kind, now);
  animFrameAt = now - kAnimFrameMs;
  touchBacklight(now, ppAnimDurationMs(kind) + kAfterAnimHoldMs);
}

// A pet reaction from a catcher event. It plays only on the home screen; with
// an old-style animation playing it is dropped, never kept for later. A dark
// screen is lit first only when the event may light it (READY); the lit time
// then covers the reaction plus the same hold as the old animations.
static void playReaction(PpPetReaction reaction, bool lightScreen, uint32_t now)
{
  if (mode != MODE_STATUS || anim.kind != PP_ANIM_NONE) {
    return;
  }
  if (!backlightOn && !lightScreen) {
    return;
  }
  // React first, so the first lit frame already shows the reaction's pose.
  ppStageReact(stage, reaction, now);
  touchBacklight(now, ppStageReactionMs(reaction) + kAfterAnimHoldMs);
}

// page 0 opens on the top page; another page opens on top of it, so Back
// returns to the top page.
static void openMenu(uint32_t now, uint8_t page = 0)
{
  anim.kind = PP_ANIM_NONE;
  if (page == 0) {
    ppMenuOpen(menuState, ppSettingsMenu(), now);
  } else {
    ppMenuOpenAt(menuState, ppSettingsMenu(), page, now);
  }
  mode = MODE_MENU;
  waitForRelease = true;
  touchBacklight(now);
  Serial.println("Display: settings menu open");
  drawMenu();
}

static void closeMenu()
{
  mode = MODE_STATUS;
  waitForRelease = true;
  // Start the lit time over from the current Screen sleep value. The timer only
  // ever extends, so without this a longer value passed through on the way to
  // a shorter one (2m, then 10s) would keep the screen lit for the long time.
  backlightUntil = millis() + screenSleepMs();
  drawStatusFrame();
}

static void switchIdentity(uint8_t index)
{
  if (index >= ppIdentityCount()) {
    return;
  }
  tft.fillScreen(kBg);
  centerText("SWITCHING TO", 80, 2, kDim);
  centerText(ppIdentityName(index), 110, 4, kAccent);
  centerText("then pair it in GO", 160, 2, kFg);
  delay(800);
  // The switch reboots; unsaved PokeGachi grants are written first.
  ppGachiFlush();
  ppIdentitySwitchTo(index);
}

// ---- PokeGachi cards and battle (MODE_GACHI) ------------------------------

// Enters MODE_GACHI with the flow already set up. `from` is the mode the flow
// returns to. The press that led here (a menu select) does nothing in the flow.
static void enterFlow(Mode from)
{
  anim.kind = PP_ANIM_NONE;
  flowReturn = from;
  mode = MODE_GACHI;
  waitForRelease = true;
  touchBacklight(millis(), playSleepMs());
  drawFlow();
}

// The card's two seconds start when it is shown, not when the action began:
// the action may have just saved to flash, which can take over a second, and
// a card timed from before the save would flash by unread.
static void showCard(const PpGachiCard& card, Mode from, uint32_t now)
{
  (void)now;
  uint32_t shownAt = millis();
  ppGachiFlowShowCard(flow, card, shownAt);
  // The sprite's animation counts from the same moment.
  cardShownAt = shownAt;
  cardPosedAt = shownAt;
  enterFlow(from);
}

// A card with the pet's sprite, or the empty ball when there is no pet. The
// lines must outlive the card: static strings or cardLine.
static PpGachiCard petCard(const char* line1, const char* line2)
{
  const PpGachiState& st = ppGachiState();
  PpGachiCard card = {};
  if (st.hasPet) {
    card.dex = st.petDex;
    card.shiny = st.petShiny;
  } else {
    card.emptyBall = true;
  }
  card.line1 = line1;
  card.line2 = line2;
  card.isArrival = false;
  return card;
}

// The new pet card. Shown only on the lit home screen; acknowledged when it closes.
static void showArrivalCard(uint32_t now)
{
  const PpGachiState& st = ppGachiState();
  if (!st.hasPet) {
    // Nothing to announce any more.
    ppGachiAckArrival();
    return;
  }
  const PpGachiSpecies* sp = ppGachiSpeciesByDex(st.petDex);
  const char* name = sp != nullptr ? sp->name : "???";
  PpGachiCard card = petCard(name, "is your new pet!");
  if (st.petShiny) {
    snprintf(cardLine, sizeof(cardLine), "A shiny %s", name);
    card.line1 = cardLine;
  }
  card.isArrival = true;
  Serial.println("Display: new pet card");
  showCard(card, MODE_STATUS, now);
}

// ---- Play and evolve moments (MODE_MOMENT) ----------------------------------
//
// A short full-stage scene on the home layout between a menu action and its
// result card: the walk for Play, the flicker for an evolve. The game result
// is applied and saved before one starts, so ending it early (a press, or the
// screen sleeping) can neither lose it nor apply it twice. Only with
// Animations on, a pet and the shared canvas; otherwise the card shows alone.

// Starts a moment that shows `card` when it ends. False, with nothing changed,
// when it cannot run; the caller then shows the card at once, as in step 1.
static bool startMoment(bool evolve, uint8_t oldDex, const PpGachiCard& card)
{
  if (!ppSettings().animations || !ppGachiState().hasPet || ppAnimCanvas() == nullptr) {
    return false;
  }
  // Timed from after the action's save, which can take over a second.
  uint32_t now = millis();
  // The stage's pet (an evolve just changed it), base and Animations setting.
  syncStage(now);
  if (evolve) {
    ppStageStartEvolve(stage, now);
  } else {
    // A display pick from the clock, not a game roll: the game's random state is untouched.
    ppStageStartPlay(stage, ppStagePlayScene(micros()), now);
  }
  if (stage.moment == PP_MOMENT_NONE) {
    // The stage turned it down (Animations off, or no pet on it): never wait on it.
    return false;
  }
  stageOldDex = evolve ? oldDex : 0;
  momentCard = card;
  anim.kind = PP_ANIM_NONE;
  mode = MODE_MOMENT;
  // The menu press that chose the action does nothing in the moment.
  waitForRelease = true;
  touchBacklight(now, playSleepMs());
  Serial.println(evolve ? "Display: evolve moment" : "Display: play moment");
  // The home layout, its stage already in the moment's first pose.
  drawStatusFrame();
  return true;
}

// Ends the moment and forgets it, so the stage reacts again afterwards.
static void endMoment()
{
  ppStageCancel(stage);
  stageOldDex = 0;
}

// Feed, Play, Battle and Evolve rows of the PokeGachi page.
static void runGachiAction(uint8_t id, uint32_t now)
{
  if (!ppGachiEnabled()) {
    // The action rows do nothing while PokeGachi is off.
    return;
  }
  const PpGachiState& st = ppGachiState();
  const char* line1 = "";
  const char* line2 = "";
  if (id == PP_MENU_ACTION_GACHI_BATTLE) {
    uint8_t opponent = 0;
    if (ppGachiBattleOpponent(opponent)) {
      ppGachiFlowStartBattle(flow, opponent);
      Serial.println("Display: battle");
      enterFlow(MODE_MENU);
      return;
    }
    ppGachiResultText(PP_GACHI_ACT_BATTLE, st.hasPet ? PP_GACHI_NO_OPPONENT : PP_GACHI_NO_PET, line1, line2);
    showCard(petCard(line1, line2), MODE_MENU, now);
    return;
  }
  bool hadPet = st.hasPet;
  uint8_t dexBefore = st.petDex;
  bool shinyBefore = st.petShiny;
  PpGachiAction action;
  PpGachiResult result;
  switch (id) {
    case PP_MENU_ACTION_GACHI_FEED: {
      action = PP_GACHI_ACT_FEED;
      result = ppGachiDoFeed();
      break;
    }
    case PP_MENU_ACTION_GACHI_PLAY: {
      action = PP_GACHI_ACT_PLAY;
      result = ppGachiDoPlay();
      break;
    }
    case PP_MENU_ACTION_GACHI_ADVANCE: {
      action = PP_GACHI_ACT_ADVANCE;
      result = ppGachiDoAdvance();
      break;
    }
    default: {
      return;
    }
  }
  // The result is applied and saved. Everything below only shows it.
  ppGachiResultText(action, result, line1, line2);
  PpGachiCard card = petCard(line1, line2);
  if (hadPet && !st.hasPet) {
    // Retired: the card shows the pet that left, not the empty ball.
    card.dex = dexBefore;
    card.shiny = shinyBefore;
    card.emptyBall = false;
  }
  bool animations = ppSettings().animations;
  if (action == PP_GACHI_ACT_FEED && result == PP_GACHI_OK && animations) {
    // The pet eats, a berry beside it. Refusals idle; Animations off is step 1's still.
    card.spriteAnim = PP_SANIM_EAT;
    card.berry = true;
  }
  // Play with a pet shows the walk whether or not it counted; only an evolve
  // shows the flicker. Each falls back to the card alone when it cannot run.
  if (hadPet && action == PP_GACHI_ACT_PLAY && startMoment(false, 0, card)) {
    return;
  }
  if (hadPet && action == PP_GACHI_ACT_ADVANCE && result == PP_GACHI_EVOLVED && startMoment(true, dexBefore, card)) {
    return;
  }
  showCard(card, MODE_MENU, now);
}

// Leaves the flow for the screen it came from. waitForRelease makes the press
// that closed the card do nothing else there.
static void returnFromFlow(uint32_t now)
{
  mode = flowReturn;
  waitForRelease = true;
  if (mode == MODE_MENU) {
    ppMenuKeepAlive(menuState, now);
    drawMenu();
  } else {
    // Back on the home screen: the normal Screen sleep time applies again.
    backlightUntil = millis() + screenSleepMs();
    drawHomeScreen();
  }
}

static void finishBattle(uint32_t now)
{
  bool won = flow.battle.won;
  uint8_t experienceBefore = ppGachiState().experience;
  ppGachiBattleFinish(won);
  const char* line1 = "You lost";
  const char* line2 = "Try again";
  if (won) {
    line1 = "You won!";
    // Empty when the track was already full: the win counts, the point does not.
    line2 = ppGachiState().experience > experienceBefore ? "Experience up" : "";
  }
  showCard(petCard(line1, line2), flowReturn, now);
}

// B = next, A = select, read through the flow each loop; both down does nothing.
static void updateFlow(uint32_t now)
{
  bool nextEdge = buttonB.pressedEdge;
  bool selectEdge = buttonA.pressedEdge;
  if (buttonA.down && buttonB.down) {
    nextEdge = false;
    selectEdge = false;
  }
  // The opponent's pick is drawn only on the loop where this A press throws.
  PpGachiPick theirPick = PP_PICK_FIRE;
  if (flow.screen == PP_GSCREEN_BATTLE && !flow.roundShowing && selectEdge && !nextEdge) {
    theirPick = ppGachiOpponentPick();
  }
  switch (ppGachiFlowUpdate(flow, nextEdge, selectEdge, theirPick, now)) {
    case PP_GFLOW_REDRAW: {
      drawFlow();
      break;
    }
    case PP_GFLOW_CARD_DONE: {
      if (flow.card.isArrival) {
        ppGachiAckArrival();
      }
      returnFromFlow(now);
      break;
    }
    case PP_GFLOW_BATTLE_DONE: {
      finishBattle(now);
      break;
    }
    default: {
      break;
    }
  }
}

// One loop of a moment. It ends when its time is up or on any press, then its
// result card shows and returns to the menu page as in step 1. The press that
// skips is consumed: the card's waitForRelease keeps it from closing the card.
static void updateMoment(uint32_t now)
{
  if (buttonA.pressedEdge || buttonB.pressedEdge || ppStageMomentDone(stage, now)) {
    endMoment();
    Serial.println("Display: moment over");
    showCard(momentCard, MODE_MENU, now);
    return;
  }
  // Catcher events during the moment changed the counters or the link word:
  // the home lines under the stage follow, as on the home screen.
  if (statusDirty || (countdownVisible() && sleepSecondsLeft(now) != countdownShown)) {
    drawStatusLines();
  }
  tickStage(now);
}

// Screen sleep during a moment: the moment is dropped and no card shows, as a
// card is dropped today; the result was saved before the moment began. The
// menu under it closes, so the screen wakes on the home screen.
static void leaveMomentForSleep()
{
  Serial.println("Display: moment dropped for sleep");
  endMoment();
  menuState.open = false;
  mode = MODE_STATUS;
  statusDirty = true;
}

// Screen sleep while a card or battle shows. A battle is abandoned with no
// result; a card just closes, and an arrival card stays pending so it shows
// again on the next lit home screen. A menu under it closes too, so the screen
// wakes on the home screen as after a menu timeout. Nothing is drawn.
static void leaveFlowForSleep()
{
  if (flow.screen == PP_GSCREEN_BATTLE) {
    Serial.println("Display: battle abandoned");
  }
  ppGachiFlowAbandon(flow);
  menuState.open = false;
  mode = MODE_STATUS;
  statusDirty = true;
}

// The screen sleep timer, unchanged by PokeGachi: presses extend it as always.
static void sleepIfDue(uint32_t now)
{
  if (!backlightOn || static_cast<int32_t>(now - backlightUntil) < 0) {
    return;
  }
  if (mode == MODE_GACHI) {
    leaveFlowForSleep();
  } else if (mode == MODE_MOMENT) {
    leaveMomentForSleep();
  } else if (mode == MODE_MENU) {
    // A card closed back to the menu on the loop the timer ran out: the menu
    // never stays open under a dark screen.
    menuState.open = false;
    mode = MODE_STATUS;
    statusDirty = true;
  }
  setBacklight(false);
}

// The new pet card waits for the lit home screen with no animation playing
// (checked by the caller) and no button in use.
static bool arrivalCardDue()
{
  return ppGachiEnabled() && ppGachiState().arrivalPending && !buttonA.down && !buttonB.down && !comboArmed &&
         !waitForRelease;
}

static void applyMenuResult(PpMenuResult result, uint32_t now)
{
  switch (result.outcome) {
    case PP_MENU_OUT_REDRAW: {
      drawMenu();
      break;
    }
    case PP_MENU_OUT_CLOSED: {
      closeMenu();
      break;
    }
    case PP_MENU_OUT_ACTION: {
      switch (result.id) {
        case PP_MENU_ACTION_IDENTITY: {
          switchIdentity(result.arg);
          break;
        }
        case PP_MENU_ACTION_GACHI_FEED:
        case PP_MENU_ACTION_GACHI_PLAY:
        case PP_MENU_ACTION_GACHI_BATTLE:
        case PP_MENU_ACTION_GACHI_ADVANCE: {
          runGachiAction(result.id, now);
          break;
        }
        case PP_MENU_ACTION_GACHI_RESET_NO: {
          applyMenuResult(ppMenuInput(menuState, ppSettingsMenu(), PP_MENU_IN_BACK, now), now);
          break;
        }
        case PP_MENU_ACTION_GACHI_RESET_YES: {
          ppGachiResetGame();
          Serial.println("Display: PokeGachi reset");
          applyMenuResult(ppMenuInput(menuState, ppSettingsMenu(), PP_MENU_IN_BACK, now), now);
          break;
        }
        default: {
          // Pokedex and Sanctuary rows: nothing to do.
          break;
        }
      }
      break;
    }
    default: {
      break;
    }
  }
}

static void updateMenu(uint32_t now)
{
  if (ppMenuTick(menuState, now).outcome == PP_MENU_OUT_CLOSED) {
    Serial.println("Display: menu timeout");
    closeMenu();
    return;
  }
  // B = next, A = select; both down does nothing. Touch boards add
  // ppMenuSelectRow() here.
  PpMenuInput input;
  if (ppMenuTwoButtonInput(buttonB.pressedEdge, buttonA.pressedEdge, buttonB.down, buttonA.down, input)) {
    applyMenuResult(ppMenuInput(menuState, ppSettingsMenu(), input, now), now);
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
  ppGachiFlowReset(flow);
  backlightUntil = millis() + screenSleepMs();
  drawHomeScreen();
  if (ppSettings().animations) {
    startAnim(PP_ANIM_BOOT, millis());
  }
  Serial.println("Display: ST7789 up");
}

void ppDisplayUpdate()
{
  if (!started) {
    return;
  }
  uint32_t now = millis();
  // A reaction belongs to the screen it began on. Once the stage is not
  // showing (menu, card, battle, dark screen, an old animation) it is dropped,
  // before a button press can wake the home screen and show the rest of it.
  if (stage.reaction != PP_REACT_NONE && !stageOnScreen()) {
    ppStageCancel(stage);
  }
  // A play or evolve moment lives only in MODE_MOMENT. Every way out ends it;
  // this is the backstop, since a moment left set would block every reaction.
  if (stage.moment != PP_MOMENT_NONE && mode != MODE_MOMENT) {
    endMoment();
  }
  readButton(buttonA, now);
  readButton(buttonB, now);

  if (buttonA.pressedEdge || buttonB.pressedEdge) {
    bool wasDark = !backlightOn;
    bool brief = mode == MODE_STATUS && !connected && everConnected;
    uint32_t lit = (mode == MODE_GACHI || mode == MODE_MOMENT) ? playSleepMs() : screenSleepMs();
    touchBacklight(now, brief ? kDisconnectedShowMs : lit);
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

  if (mode == MODE_MOMENT) {
    // The moment alone reads the buttons; a press ends it and nothing else.
    updateMoment(now);
    sleepIfDue(now);
    return;
  }

  if (mode == MODE_GACHI) {
    // The flow alone reads the buttons; a press that closes a card or ends
    // a battle is consumed there and the loop ends here.
    updateFlow(now);
    if (mode == MODE_GACHI) {
      tickCard(now);
    }
    sleepIfDue(now);
    return;
  }

  if (!waitForRelease) {
    updateCombo(now);
  }
  updateResetHold(now);
  // Five quick B taps on the home screen open the PokeGachi page, only while
  // PokeGachi is on. The fifth press waits for release inside the menu.
  if (mode == MODE_STATUS && buttonB.pressedEdge && !buttonA.down && ppGachiEnabled() &&
      ppTapCounterTap(quickTaps, now)) {
    openMenu(now, ppMenuPagePokegachi());
  }
  if (anim.kind != PP_ANIM_NONE && mode == MODE_STATUS) {
    if (ppAnimDone(anim, now)) {
      anim.kind = PP_ANIM_NONE;
      // A pet that just arrived is announced straight away, without one
      // loop of the pet face showing it first.
      if (backlightOn && !holdShown && arrivalCardDue()) {
        showArrivalCard(now);
      }
      if (mode == MODE_STATUS) {
        drawHomeScreen();
      }
    } else if (now - animFrameAt >= kAnimFrameMs) {
      animFrameAt = now;
      if (!ppAnimDrawFrame(tft, anim.kind, ppAnimProgress(anim, now))) {
        anim.kind = PP_ANIM_NONE;
        drawStatusFrame();
      }
    }
  } else if (mode == MODE_STATUS && !holdShown && backlightOn) {
    if (arrivalCardDue()) {
      showArrivalCard(now);
    } else if (statusDirty) {
      drawStatusLines();
    } else if (countdownVisible() && sleepSecondsLeft(now) != countdownShown) {
      if (ppGachiEnabled()) {
        // The pet face prints its lines without a background: redraw the band.
        drawStatusLines();
      } else {
        drawCountdown(now);
      }
    }
  }
  // The stage sits above the lines band, clear of the reset bar, so it keeps
  // moving while A is held. Nothing ticks on a dark screen or with Animations off.
  if (stageOnScreen() && ppSettings().animations) {
    tickStage(now);
  }
  sleepIfDue(now);
}

void ppDisplayOnEvent(PpFeedbackEvent event)
{
  // First, so a screen lit by the disconnect below already shows the pet asleep.
  goReady = ppStageGoReadyAfter(goReady, event);
  bool linesChanged = true;
  switch (event) {
    case PP_FEEDBACK_CONNECTED: {
      connected = true;
      everConnected = true;
      lastEvent = "";
      lastEventWarns = false;
      break;
    }
    case PP_FEEDBACK_DISCONNECTED: {
      connected = false;
      if (everConnected) {
        // The state word says it; show the screen briefly, then go dark.
        lastEvent = "";
        lastEventWarns = false;
        if (started && mode == MODE_STATUS) {
          touchBacklight(millis(), kDisconnectedShowMs);
        }
      }
      break;
    }
    case PP_FEEDBACK_CAUGHT: {
      ++caught;
      lastEvent = "caught!";
      lastEventWarns = false;
      lastEventColor = kGood;
      break;
    }
    case PP_FEEDBACK_FLED: {
      ++fled;
      lastEvent = "fled";
      lastEventWarns = false;
      lastEventColor = kBad;
      break;
    }
    case PP_FEEDBACK_POKESTOP_SPUN: {
      ++spun;
      lastEvent = "spun";
      lastEventWarns = false;
      lastEventColor = ST77XX_CYAN;
      break;
    }
    case PP_FEEDBACK_POKEMON_FULL: {
      lastEvent = "POKEMON FULL";
      lastEventWarns = true;
      lastEventColor = kAccent;
      break;
    }
    case PP_FEEDBACK_READY: {
      connected = true;
      everConnected = true;
      lastEvent = "ready!";
      lastEventWarns = false;
      lastEventColor = ST77XX_YELLOW;
      break;
    }
    case PP_FEEDBACK_BAG_FULL: {
      lastEvent = "BAG FULL";
      lastEventWarns = true;
      lastEventColor = ST77XX_CYAN;
      break;
    }
    // The next two never light a dark screen; the text is there to check the
    // decode against the game when the screen is already lit.
    case PP_FEEDBACK_SPIN_FAILED: {
      lastEvent = "spin failed";
      lastEventWarns = false;
      lastEventColor = kDim;
      break;
    }
    case PP_FEEDBACK_STOP_PASSED: {
      lastEvent = "stop passed";
      lastEventWarns = false;
      lastEventColor = kDim;
      break;
    }
    case PP_FEEDBACK_BALLS_EMPTY: {
      lastEvent = "NO BALLS";
      lastEventWarns = true;
      lastEventColor = kBad;
      break;
    }
    case PP_FEEDBACK_POKEMON_NEARBY: {
      // No text and no count: only the pet notices it. With PokeGachi off it
      // does nothing, as before.
      if (!ppGachiEnabled()) {
        return;
      }
      linesChanged = false;
      break;
    }
    default: {
      return;
    }
  }
  if (linesChanged) {
    statusDirty = true;
  }
  // The pet's base state follows every event, on screen or not.
  syncStage(millis());
  // The menu, a PokeGachi card or a battle is never drawn over: the counts
  // above are kept and the home screen shows them when it comes back. A pet
  // reaction for such a moment is dropped, never played later.
  if (!started || mode != MODE_STATUS) {
    return;
  }
  // Dark screen: catches, flees and spins only count. Tap a button to look.
  // With the Animations setting off nothing plays and only warnings light the
  // screen. With PokeGachi on the pet reacts instead of the old animations.
  const PpGachiState& st = ppGachiState();
  PpScreenContext ctx = {};
  ctx.backlightOn = backlightOn;
  ctx.animationsOn = ppSettings().animations;
  ctx.gachiOn = ppGachiEnabled();
  // A pet whose arrival card is still to come is not on the stage yet: the
  // card is that catch's reaction.
  ctx.petOnStage = st.hasPet && !st.arrivalPending;
  ctx.berryRefused = event == PP_FEEDBACK_POKESTOP_SPUN && ppGachiLastGrant() != PP_GACHI_GRANT_BERRY;
  PpScreenAction action = ppEventScreenActionFor(event, ctx);
  uint32_t now = millis();
  switch (action.kind) {
    case PP_SCREEN_ANIM: {
      startAnim(action.anim, now);
      break;
    }
    case PP_SCREEN_PET_REACTION: {
      playReaction(action.reaction, action.lightScreen, now);
      break;
    }
    case PP_SCREEN_LIGHT_HOME: {
      if (!backlightOn) {
        touchBacklight(now);
      }
      break;
    }
    case PP_SCREEN_LIGHT_WARNING: {
      touchBacklight(now, kWarningShowMs);
      break;
    }
    default: {
      break;
    }
  }
}

void ppDisplayOnFactoryReset()
{
  if (!started) {
    return;
  }
  connected = false;
  everConnected = false;
  goReady = false;
  caught = 0;
  fled = 0;
  spun = 0;
  lastEvent = "RESET - pair in GO";
  lastEventWarns = true;
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
  return mode == MODE_MENU || mode == MODE_GACHI || mode == MODE_MOMENT || buttonB.down || comboArmed ||
         waitForRelease;
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
