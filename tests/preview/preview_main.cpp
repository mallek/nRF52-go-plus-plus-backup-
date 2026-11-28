// PC preview of the PokeGachi drawing code. Renders the real pp_gachi_ui and
// pp_gachi_stage_draw code into an in-memory RGB565 canvas and writes one PPM file
// per still, plus a folder of frames per animated preview (gif_<name>/ with a
// frames.txt listing each frame and how long it shows). The animated previews are
// driven by the real stage state machine at 50 ms steps, and a frame is redrawn
// only when ppStagePose reports a change, as on the device.
// Built and run by render.sh; see there for the usage.

#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "Arduino.h"
#include "Adafruit_GFX.h"
#include "pp_gachi_battle.h"
#include "pp_gachi_flow.h"
#include "pp_gachi_logic.h"
#include "pp_gachi_species.h"
#include "pp_gachi_sprites.h"
#include "pp_gachi_stage.h"
#include "pp_gachi_stage_draw.h"
#include "pp_gachi_text.h"
#include "pp_gachi_ui.h"
#include "pp_menu_draw.h"
#include "pp_menu_items.h"
#include "pp_menu_logic.h"
#include "menu_stubs.h"
#include "preview_sprites.h"

namespace {

class Canvas : public Adafruit_GFX {
 public:
  Canvas(int16_t w, int16_t h) : Adafruit_GFX(w, h), px_(static_cast<size_t>(w) * h, 0) {}
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= width() || y >= height()) {
      return;
    }
    px_[static_cast<size_t>(y) * width() + x] = color;
  }
  uint16_t at(int16_t x, int16_t y) const { return px_[static_cast<size_t>(y) * width() + x]; }
  void fillAll(uint16_t color) { std::fill(px_.begin(), px_.end(), color); }
  bool writePpm(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
      return false;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", width(), height());
    for (uint16_t c : px_) {
      unsigned char rgb[3];
      rgb[0] = static_cast<unsigned char>(((c >> 11) & 0x1F) * 255 / 31);
      rgb[1] = static_cast<unsigned char>(((c >> 5) & 0x3F) * 255 / 63);
      rgb[2] = static_cast<unsigned char>((c & 0x1F) * 255 / 31);
      std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
    return true;
  }

 private:
  std::vector<uint16_t> px_;
};

const PpMenuColors kColors = {0x0000, 0xFFFF, 0x7BEF, 0xFD20};
const uint16_t kGood = 0x07E0;
const uint16_t kBad = 0xF800;

std::string gOut;
int gFailed = 0;

#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond); \
      gFailed = 1; \
    } \
  } while (0)

void save(const Canvas& c, const char* name) {
  std::string path = gOut + "/" + name + ".ppm";
  if (!c.writePpm(path)) {
    std::printf("cannot write %s\n", path.c_str());
    gFailed = 1;
  }
}

PpGachiState basePet(uint8_t dex, bool shiny) {
  PpGachiState st;
  ppGachiInit(st, 1234);
  st.hasPet = true;
  st.petDex = dex;
  st.petShiny = shiny;
  st.berries = 3;
  st.opponentCount = 1;
  st.dexRaised = 1;
  return st;
}

PpGachiHomeLines normalLines() {
  PpGachiHomeLines l;
  l.topLeft = "TRAVIS 1/3";
  l.topRight = "CONNECTED";
  l.topRightColor = kGood;
  l.pantryOverride = nullptr;
  l.bottom = "caught 12  fled 3  spun 8";
  l.bottomColor = kColors.dim;
  return l;
}

PpGachiHomeLines asleepLines() {
  PpGachiHomeLines l = normalLines();
  l.topRight = "DISCONNECTED";
  l.topRightColor = kBad;
  l.bottom = "DISCONNECTED";
  l.bottomColor = kBad;
  return l;
}

// ---------------------------------------------------------------- the stage

// The device's shared off-screen canvas is 240x200; the stage is drawn into one of
// the same size and its sw x sh corner is copied to the stage rectangle, as the
// device pushes it. Everything outside that corner is filled with a marker first
// and checked afterwards, so a scene or symbol drawn off the stage is caught.
constexpr uint16_t kMarker = 0x1234;
int gOffStage = 0;

void drawStage(Canvas& screen, int16_t w, int16_t h, const PpStagePose& pose, uint8_t dex, bool shiny, uint8_t oldDex) {
  int16_t x = 0;
  int16_t y = 0;
  int16_t sw = 0;
  int16_t sh = 0;
  ppGachiStageRect(w, h, x, y, sw, sh);
  CHECK(sw <= 240 && sh <= 200);
  Canvas stage(240, 200);
  stage.fillAll(kMarker);
  ppGachiStageDraw(stage, sw, sh, pose, dex, shiny, oldDex, kColors);
  int before = gOffStage;
  int16_t minX = 999;
  int16_t minY = 999;
  int16_t maxX = -1;
  int16_t maxY = -1;
  for (int16_t yy = 0; yy < 200; ++yy) {
    for (int16_t xx = 0; xx < 240; ++xx) {
      if (xx < sw && yy < sh) {
        screen.drawPixel(static_cast<int16_t>(x + xx), static_cast<int16_t>(y + yy), stage.at(xx, yy));
      } else if (stage.at(xx, yy) != kMarker) {
        ++gOffStage;
        minX = std::min(minX, xx);
        minY = std::min(minY, yy);
        maxX = std::max(maxX, xx);
        maxY = std::max(maxY, yy);
      }
    }
  }
  if (gOffStage != before) {
    std::printf("off stage %dx%d scene %d symbol %d: x %d..%d y %d..%d\n", sw, sh, pose.scene, pose.symbol, minX, maxX, minY,
                maxY);
  }
}

PpStageTimings timingsFor(uint8_t dex, bool shiny) {
  PpStageTimings t;
  ppGachiSpriteTimings(ppGachiSpriteFor(dex, shiny), t);
  return t;
}

// The pose a freshly drawn home screen shows for a base state.
PpStagePose stillPose(PpStageBase base, uint8_t dex, bool shiny) {
  PpStage st;
  ppStageInit(st, 1000);
  ppStageSetBase(st, base, 1000);
  PpStagePose p;
  ppStagePose(st, timingsFor(dex, shiny), 1000, p);
  return p;
}

void homeWithStage(Canvas& c, int16_t w, int16_t h, const PpGachiState& st, const PpGachiHomeLines& lines, PpStageBase base) {
  ppGachiDrawHome(c, w, h, st, lines, kColors);
  drawStage(c, w, h, stillPose(base, st.petDex, st.petShiny), st.petDex, st.petShiny, 0);
}

// ---------------------------------------------------------------- animated previews

constexpr uint32_t kStepMs = 50;
constexpr uint32_t kStart = 600000;   // ten minutes after boot

// Frames of one animated preview: gif_<name>/fNNNN.ppm and frames.txt ("file ms" per line).
class Sequence {
 public:
  explicit Sequence(const std::string& name) : name_(name), dir_(gOut + "/gif_" + name) {
    mkdir(dir_.c_str(), 0755);
  }
  void frame(const Canvas& c) {
    char file[16];
    std::snprintf(file, sizeof(file), "f%04d.ppm", static_cast<int>(files_.size()));
    if (!c.writePpm(dir_ + "/" + file)) {
      std::printf("cannot write %s/%s\n", dir_.c_str(), file);
      gFailed = 1;
    }
    files_.push_back(file);
    ms_.push_back(kStepMs);
  }
  void hold() {
    if (!ms_.empty()) {
      ms_.back() += kStepMs;
    }
  }
  size_t frames() const { return files_.size(); }
  void finish() {
    FILE* f = std::fopen((dir_ + "/frames.txt").c_str(), "w");
    if (f == nullptr) {
      gFailed = 1;
      return;
    }
    for (size_t i = 0; i < files_.size(); ++i) {
      std::fprintf(f, "%s %u\n", files_[i].c_str(), static_cast<unsigned>(ms_[i]));
    }
    std::fclose(f);
    std::printf("%-28s %3u frame(s)\n", name_.c_str(), static_cast<unsigned>(files_.size()));
  }

 private:
  std::string name_;
  std::string dir_;
  std::vector<std::string> files_;
  std::vector<uint32_t> ms_;
};

struct StageRun {
  int16_t w = 240;
  int16_t h = 240;
  PpGachiState game;
  PpGachiHomeLines lines;
  uint8_t oldDex = 0;
  uint32_t lengthMs = 3000;
  std::function<void(PpStage&, uint32_t)> setup;   // called once at kStart
  std::function<void(PpStage&, uint32_t)> event;   // called every step before the pose
};

// Draws the home face once, then asks the real stage for a pose every 50 ms and
// redraws the stage only when the pose changed. Returns the frame count.
size_t recordStage(const std::string& name, const StageRun& run) {
  Sequence seq(name);
  Canvas screen(run.w, run.h);
  ppGachiDrawHome(screen, run.w, run.h, run.game, run.lines, kColors);
  PpStage st;
  ppStageInit(st, kStart);
  if (run.setup) {
    run.setup(st, kStart);
  }
  PpStageTimings timings = timingsFor(run.game.petDex, run.game.petShiny);
  for (uint32_t t = kStart; t < kStart + run.lengthMs; t += kStepMs) {
    if (run.event) {
      run.event(st, t);
    }
    PpStagePose pose;
    bool changed = ppStagePose(st, timings, t, pose);
    if (changed) {
      drawStage(screen, run.w, run.h, pose, run.game.petDex, run.game.petShiny, run.oldDex);
      seq.frame(screen);
    } else {
      seq.hold();
    }
  }
  seq.finish();
  return seq.frames();
}

StageRun petRun(PpStageBase base) {
  StageRun r;
  r.game = basePet(4, false);
  r.game.nutrition = 4;
  r.game.happiness = 2;
  r.game.experience = 3;
  r.lines = base == PP_STAGE_ASLEEP ? asleepLines() : normalLines();
  r.setup = [base](PpStage& st, uint32_t now) { ppStageSetBase(st, base, now); };
  return r;
}

StageRun reactionRun(PpPetReaction reaction, PpStageBase base) {
  StageRun r = petRun(base);
  const uint32_t at = kStart + 300;
  r.lengthMs = 300 + ppStageReactionMs(reaction) + 500;
  r.event = [reaction, at](PpStage& st, uint32_t now) {
    if (now == at) {
      ppStageReact(st, reaction, now);
    }
  };
  return r;
}

void renderStageGifs() {
  // Home idle, at both screen sizes.
  {
    // Twelve seconds of the idle wander, starting the way a wake does.
    StageRun r = petRun(PP_STAGE_IDLE);
    r.lengthMs = 12000;
    r.setup = [](PpStage& st, uint32_t now) {
      ppStageSetBase(st, PP_STAGE_IDLE, now);
      st.rng = 2024;
      ppStageNudge(st, now);
    };
    recordStage("home_idle", r);
  }
  {
    StageRun r = petRun(PP_STAGE_IDLE);
    r.w = 172;
    r.h = 320;
    recordStage("home_idle_172x320", r);
  }
  {
    StageRun r = petRun(PP_STAGE_ASLEEP);
    r.lengthMs = 4000;
    recordStage("home_asleep", r);
  }
  recordStage("react_happy", reactionRun(PP_REACT_HAPPY, PP_STAGE_IDLE));
  recordStage("react_flop", reactionRun(PP_REACT_FLOP, PP_STAGE_IDLE));
  recordStage("react_berry", reactionRun(PP_REACT_BERRY, PP_STAGE_IDLE));
  recordStage("react_berry_full", reactionRun(PP_REACT_BERRY_FULL, PP_STAGE_IDLE));
  recordStage("react_alert", reactionRun(PP_REACT_ALERT, PP_STAGE_IDLE));
  {
    StageRun r = reactionRun(PP_REACT_WAKE, PP_STAGE_ASLEEP);
    r.lines = normalLines();
    recordStage("react_wake", r);
  }
  {
    StageRun r = reactionRun(PP_REACT_HAPPY, PP_STAGE_IDLE);
    r.w = 172;
    r.h = 320;
    recordStage("react_happy_172x320", r);
  }

  const PpStageScene scenes[PP_SCENE_PLAY_COUNT] = {PP_SCENE_MEADOW, PP_SCENE_BEACH, PP_SCENE_FOREST, PP_SCENE_CAMP_NIGHT};
  const char* sceneNames[PP_SCENE_PLAY_COUNT] = {"meadow", "beach", "forest", "camp"};
  for (uint8_t i = 0; i < PP_SCENE_PLAY_COUNT; ++i) {
    StageRun r = petRun(PP_STAGE_IDLE);
    PpStageScene scene = scenes[i];
    r.lengthMs = PP_STAGE_PLAY_MS + 300;
    r.setup = [scene](PpStage& st, uint32_t now) {
      ppStageSetBase(st, PP_STAGE_IDLE, now);
      ppStageStartPlay(st, scene, now);
    };
    recordStage(std::string("play_") + sceneNames[i], r);
    if (i == 0) {
      r.w = 172;
      r.h = 320;
      recordStage("play_meadow_172x320", r);
    }
  }
  {
    StageRun r = petRun(PP_STAGE_IDLE);
    r.game = basePet(2, false);   // evolving from dex 1 into dex 2
    r.oldDex = 1;
    r.lengthMs = PP_STAGE_EVOLVE_MS + 300;
    r.setup = [](PpStage& st, uint32_t now) {
      ppStageSetBase(st, PP_STAGE_IDLE, now);
      ppStageStartEvolve(st, now);
    };
    recordStage("evolve", r);
  }
  {
    StageRun r;
    ppGachiInit(r.game, 99);
    r.lines = normalLines();
    r.lengthMs = 1000;
    recordStage("home_nopet", r);
  }
  {
    StageRun r = petRun(PP_STAGE_IDLE);
    r.setup = [](PpStage& st, uint32_t now) {
      ppStageSetBase(st, PP_STAGE_IDLE, now);
      ppStageSetAnimations(st, false);
    };
    // Reactions arrive too; with Animations OFF they must change nothing.
    r.event = [](PpStage& st, uint32_t now) {
      if (now == kStart + 500) {
        ppStageReact(st, PP_REACT_HAPPY, now);
      }
    };
    size_t frames = recordStage("home_anim_off", r);
    CHECK(frames == 1);
  }
}

// ppGachiDrawFrame on a tiny lopsided sprite (the dummy art is round, so it cannot show
// mirroring or tipping). Frame, 3 wide and 2 tall, ground under row 1:
//   row 0:  A . .     (the "head", at the left)
//   row 1:  B B B     (the "feet")
void checkFrameDrawing() {
  static const uint8_t kPix[] = {0x10, 0x00, 0x22, 0x20};
  static const uint16_t kMs[] = {100};
  static uint16_t pal[16] = {0};
  pal[1] = 0xAAAA;
  pal[2] = 0xBBBB;
  PpGachiSprite spr = {};
  spr.palette = pal;
  spr.anim[PP_SANIM_IDLE] = {3, 2, 1, 1, 2, kMs, kPix};
  const PpGachiAnim* a = &spr.anim[PP_SANIM_IDLE];
  const uint16_t A = 0xAAAA;
  const uint16_t B = 0xBBBB;
  {
    Canvas c(20, 30);
    ppGachiDrawFrame(c, 10, 20, 1, &spr, a, 0, false, false);
    CHECK(c.at(9, 18) == A && c.at(10, 18) == 0 && c.at(11, 18) == 0);
    CHECK(c.at(9, 19) == B && c.at(10, 19) == B && c.at(11, 19) == B);
    CHECK(c.at(10, 20) == 0);   // nothing under the ground line
  }
  {
    Canvas c(20, 30);
    ppGachiDrawFrame(c, 10, 20, 1, &spr, a, 0, true, false);
    // Mirrored about the line originX columns in from the crop's left edge (x = 10 here).
    CHECK(c.at(10, 18) == A && c.at(9, 18) == 0 && c.at(11, 18) == 0);
    CHECK(c.at(8, 19) == B && c.at(9, 19) == B && c.at(10, 19) == B && c.at(11, 19) == 0);
  }
  {
    // On its side, a quarter turn clockwise: head to the right, feet to the left,
    // the frame's right edge resting on the ground line.
    Canvas c(20, 30);
    ppGachiDrawFrame(c, 10, 20, 1, &spr, a, 0, false, true);
    CHECK(c.at(10, 17) == A && c.at(10, 18) == 0 && c.at(10, 19) == 0);
    CHECK(c.at(9, 17) == B && c.at(9, 18) == B && c.at(9, 19) == B);
    CHECK(c.at(9, 20) == 0 && c.at(10, 20) == 0);
  }
  {
    Canvas c(20, 30);
    ppGachiDrawFrame(c, 10, 20, 1, &spr, a, 0, true, true);
    CHECK(c.at(9, 17) == A && c.at(10, 17) == B && c.at(10, 19) == B);
  }
  {
    // Scale 2: each pixel a 2x2 block, still standing on the ground line.
    Canvas c(30, 30);
    ppGachiDrawFrame(c, 10, 20, 2, &spr, a, 0, false, false);
    CHECK(c.at(8, 16) == A && c.at(9, 17) == A && c.at(10, 16) == 0);
    CHECK(c.at(8, 19) == B && c.at(13, 19) == B && c.at(8, 20) == 0);
  }
  CHECK(ppGachiSpriteScale(&spr, 9, 6) == 3);
  CHECK(ppGachiSpriteScale(&spr, 2, 6) == 0);
  CHECK(ppGachiSpriteScale(nullptr, 100, 100) == 0);
}

// A screen that counts drawRGBBitmap calls and keeps what they draw.
struct PushScreen {
  Canvas pixels{320, 320};
  int calls = 0;
  void drawRGBBitmap(int16_t x, int16_t y, uint16_t* buf, int16_t w, int16_t h) {
    ++calls;
    pixels.drawRGBBitmap(x, y, buf, w, h);
  }
};

// ppGachiStagePush from a 240x200 canvas, as on the device: a full-width stage goes in
// one call, a 172-wide one row by row with the 240 stride, and every pixel lands right.
void checkStagePush() {
  GFXcanvas16 canvas(240, 200);
  CHECK(canvas.getBuffer() != nullptr);
  for (int16_t y = 0; y < 200; ++y) {
    for (int16_t x = 0; x < 240; ++x) {
      canvas.drawPixel(x, y, static_cast<uint16_t>(y * 240 + x + 1));
    }
  }
  const int16_t sizes[2][2] = {{240, 120}, {172, 164}};
  for (uint8_t s = 0; s < 2; ++s) {
    int16_t sw = sizes[s][0];
    int16_t sh = sizes[s][1];
    PushScreen screen;
    CHECK(ppGachiStagePush(screen, canvas, 3, 14, sw, sh));
    CHECK(screen.calls == (sw == 240 ? 1 : sh));
    int bad = 0;
    for (int16_t y = 0; y < sh; ++y) {
      for (int16_t x = 0; x < sw; ++x) {
        if (screen.pixels.at(static_cast<int16_t>(3 + x), static_cast<int16_t>(14 + y)) != static_cast<uint16_t>(y * 240 + x + 1)) {
          ++bad;
        }
      }
    }
    CHECK(bad == 0);
    CHECK(screen.pixels.at(static_cast<int16_t>(3 + sw), 14) == 0);    // nothing right of the area
    CHECK(screen.pixels.at(3, static_cast<int16_t>(14 + sh)) == 0);    // nothing under it
  }
  PushScreen screen;
  CHECK(!ppGachiStagePush(screen, canvas, 0, 0, 241, 10));
  CHECK(!ppGachiStagePush(screen, canvas, 0, 0, 100, 201));
  CHECK(!ppGachiStagePush(screen, canvas, 0, 0, 0, 10));
  CHECK(screen.calls == 0);
}

// Every scene behind the idle pet, and the placeholder for a species with no sprite.
void renderStageStills() {
  const PpStageScene scenes[PP_SCENE_COUNT] = {PP_SCENE_MEADOW, PP_SCENE_BEACH, PP_SCENE_FOREST, PP_SCENE_CAMP_NIGHT,
                                               PP_SCENE_MEADOW_NIGHT};
  const char* names[PP_SCENE_COUNT] = {"scene_meadow", "scene_beach", "scene_forest", "scene_camp", "scene_meadow_night"};
  const int16_t sizes[2][2] = {{240, 240}, {172, 320}};
  for (uint8_t s = 0; s < 2; ++s) {
    int16_t w = sizes[s][0];
    int16_t h = sizes[s][1];
    for (uint8_t i = 0; i < PP_SCENE_COUNT; ++i) {
      Canvas c(w, h);
      PpGachiState st = basePet(4, false);
      ppGachiDrawHome(c, w, h, st, normalLines(), kColors);
      PpStagePose pose = stillPose(PP_STAGE_IDLE, 4, false);
      pose.scene = scenes[i];
      drawStage(c, w, h, pose, 4, false, 0);
      save(c, (std::string(names[i]) + (s == 0 ? "" : "_172x320")).c_str());
    }
  }
  Canvas c(240, 240);
  PpGachiState st = basePet(99, false);   // no such species: no sprite
  ppGachiDrawHome(c, 240, 240, st, normalLines(), kColors);
  drawStage(c, 240, 240, stillPose(PP_STAGE_IDLE, 99, false), 99, false, 0);
  save(c, "home_placeholder");
}

// A feed card whose sprite plays its animation. The card's frame clock is the real
// stage: a stage whose Idle timing is the card animation's timing loops it exactly as
// the home screen loops Idle, so the frame shown at each time comes from ppStagePose.
size_t recordCard(const std::string& name, const PpGachiCard& card, int16_t w, int16_t h) {
  Sequence seq(name);
  Canvas c(w, h);
  ppGachiDrawCard(c, w, h, card, kColors);
  const PpGachiSprite* spr = ppGachiSpriteFor(card.dex, card.shiny);
  const PpGachiAnim* anim = ppGachiSpriteAnim(spr, static_cast<PpStageAnim>(card.spriteAnim));
  PpStageTimings timings = {};
  if (anim != nullptr) {
    timings.anim[PP_SANIM_IDLE].frames = anim->frames;
    timings.anim[PP_SANIM_IDLE].ms = anim->ms;
  }
  PpStage st;
  ppStageInit(st, kStart);
  ppStageSetBase(st, PP_STAGE_IDLE, kStart);
  int lastFrame = -1;
  for (uint32_t t = kStart; t < kStart + PP_GACHI_CARD_MS; t += kStepMs) {
    PpStagePose pose;
    ppStagePose(st, timings, t, pose);
    if (pose.frame != lastFrame) {
      ppGachiDrawCardSprite(c, w, h, card, pose.frame, kColors);
      seq.frame(c);
      lastFrame = pose.frame;
    } else {
      seq.hold();
    }
  }
  seq.finish();
  return seq.frames();
}

void renderCardGifs() {
  PpGachiCard card = {};
  ppGachiResultText(PP_GACHI_ACT_FEED, PP_GACHI_OK, card.line1, card.line2);
  card.spriteAnim = PP_SANIM_EAT;
  card.berry = true;
  card.dex = 4;
  recordCard("card_feed_eat", card, 240, 240);
  // Dex 2 has no Eat in the real art; the preview hides the dummy one so the card idles.
  previewHideEat(2);
  CHECK(ppGachiSpriteAnim(ppGachiSpriteFor(2, false), PP_SANIM_EAT) == ppGachiSpriteAnim(ppGachiSpriteFor(2, false), PP_SANIM_IDLE));
  card.dex = 2;
  recordCard("card_feed_noeat", card, 240, 240);
  previewHideEat(0);
}

void renderHomes(int16_t w, int16_t h, const char* suffix) {
  auto name = [&](const char* base) { return std::string(base) + suffix; };
  {
    Canvas c(w, h);
    PpGachiState st = basePet(4, false);
    st.nutrition = 4;
    st.happiness = 2;
    st.experience = 3;
    homeWithStage(c, w, h, st, normalLines(), PP_STAGE_IDLE);
    save(c, name("home_pet").c_str());
  }
  {
    Canvas c(w, h);
    PpGachiState st = basePet(2, false);
    st.nutrition = 10;
    st.happiness = 10;
    st.experience = 10;
    homeWithStage(c, w, h, st, normalLines(), PP_STAGE_IDLE);
    save(c, name("home_ready").c_str());
  }
  {
    Canvas c(w, h);
    PpGachiState st = basePet(5, true);
    st.nutrition = 10;
    st.happiness = 7;
    st.experience = 9;
    st.berries = 10;
    st.opponentCount = 5;
    homeWithStage(c, w, h, st, normalLines(), PP_STAGE_IDLE);
    save(c, name("home_shiny_10").c_str());
  }
  {
    Canvas c(w, h);
    PpGachiState st;
    ppGachiInit(st, 99);
    homeWithStage(c, w, h, st, normalLines(), PP_STAGE_EMPTY);
    save(c, name("home_nopet_first").c_str());
  }
  {
    Canvas c(w, h);
    PpGachiState st;
    ppGachiInit(st, 99);
    st.dexRaised = 0x0007;
    st.sanctuaryCount = 1;
    st.sanctuary[0].dex = 3;
    PpGachiHomeLines l = normalLines();
    l.bottom = "POKEMON FULL";
    l.bottomColor = kBad;
    homeWithStage(c, w, h, st, l, PP_STAGE_EMPTY);
    save(c, name("home_nopet_retired").c_str());
  }
  {
    Canvas c(w, h);
    PpGachiState st = basePet(7, false);
    st.nutrition = 5;
    st.happiness = 1;
    st.experience = 0;
    PpGachiHomeLines l = normalLines();
    l.topRight = "DISCONNECTED";
    l.topRightColor = kBad;
    l.pantryOverride = "sleep in 04:32";
    l.bottom = "BAG FULL";
    l.bottomColor = kBad;
    homeWithStage(c, w, h, st, l, PP_STAGE_ASLEEP);
    save(c, name("home_countdown").c_str());
  }
}

void renderCards(int16_t w, int16_t h, const char* suffix) {
  auto name = [&](const char* base) { return std::string(base) + suffix; };
  {
    Canvas c(w, h);
    PpGachiCard card = {};
    card.dex = 4;
    card.spriteAnim = PP_SANIM_EAT;
    card.berry = true;
    ppGachiResultText(PP_GACHI_ACT_FEED, PP_GACHI_OK, card.line1, card.line2);
    ppGachiDrawCard(c, w, h, card, kColors);
    save(c, name("card_feed").c_str());
  }
  {
    Canvas c(w, h);
    PpGachiCard card = {};
    card.dex = 6;
    card.shiny = true;
    card.line1 = "A new pet! *";
    card.line2 = "CHARIZARD";
    ppGachiDrawCard(c, w, h, card, kColors);
    save(c, name("card_arrival").c_str());
  }
  {
    Canvas c(w, h);
    PpGachiCard card = {};
    card.dex = 0;
    card.emptyBall = true;
    ppGachiResultText(PP_GACHI_ACT_BATTLE, PP_GACHI_NO_PET, card.line1, card.line2);
    ppGachiDrawCard(c, w, h, card, kColors);
    save(c, name("card_nopet").c_str());
  }
  {
    Canvas c(w, h);
    PpGachiCard card = {};
    card.dex = 5;
    ppGachiResultText(PP_GACHI_ACT_PLAY, PP_GACHI_PLAY_FULL, card.line1, card.line2);
    ppGachiDrawCard(c, w, h, card, kColors);
    save(c, name("card_long").c_str());
  }
}

void renderBattles(int16_t w, int16_t h, const char* suffix) {
  auto name = [&](const char* base) { return std::string(base) + suffix; };
  {
    Canvas c(w, h);
    PpGachiBattle b;
    ppGachiBattleBegin(b, 2);
    b.myWins = 1;
    b.cursor = 1;
    ppGachiDrawBattle(c, w, h, b, false, 4, false, kColors);
    save(c, name("battle_pick").c_str());
  }
  {
    Canvas c(w, h);
    PpGachiBattle b;
    ppGachiBattleBegin(b, 5);
    ppGachiBattleNext(b);
    ppGachiBattleThrow(b, PP_PICK_FIRE);   // WATER beats FIRE
    ppGachiDrawBattle(c, w, h, b, true, 4, true, kColors);
    save(c, name("battle_round").c_str());
  }
}

bool same(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

void drawMenuScene(int16_t w, int16_t h, const PpMenuState& st, const char* name) {
  PpMenuView view;
  ppMenuBuildView(st, ppSettingsMenu(), ppMenuRowsThatFit(h), view);
  Canvas c(w, h);
  ppMenuDraw(c, w, h, view, kColors);
  save(c, name);
}

PpMenuState openGachi() {
  PpMenuState st;
  ppMenuOpenAt(st, ppSettingsMenu(), ppMenuPagePokegachi(), 1000);
  return st;
}

void nextTo(PpMenuState& st, uint8_t row) {
  for (int i = 0; i < 40 && st.cursor[st.depth] != row; ++i) {
    ppMenuInput(st, ppSettingsMenu(), PP_MENU_IN_NEXT, 1000);
  }
}

// Row values of the PokeGachi page, read through a view of every row.
void checkGachiValues() {
  const PpMenu& m = ppSettingsMenu();
  PpMenuState st = openGachi();
  PpMenuView v;
  gStubGachiEnabled = false;
  ppMenuBuildView(st, m, PP_MENU_MAX_VISIBLE, v);
  CHECK(same(v.rows[1].label, "Feed") && same(v.rows[1].value, "off"));
  CHECK(same(v.rows[2].value, ""));
  CHECK(same(v.rows[3].value, "off"));
  CHECK(same(v.rows[4].value, "off"));
  gStubGachiEnabled = true;
  ppGachiInit(gStubGachi, 7);
  ppMenuBuildView(st, m, PP_MENU_MAX_VISIBLE, v);
  CHECK(same(v.rows[1].value, "no pet") && same(v.rows[2].value, "no pet"));
  CHECK(same(v.rows[3].value, "no pet") && same(v.rows[4].value, "no pet"));
  gStubGachi.hasPet = true;
  gStubGachi.petDex = 1;  // a first form
  gStubGachi.berries = 3;
  gStubGachi.opponentCount = 2;
  ppMenuBuildView(st, m, PP_MENU_MAX_VISIBLE, v);
  CHECK(same(v.rows[1].value, "3 berries") && same(v.rows[2].value, ""));
  CHECK(same(v.rows[3].value, "2 waiting"));
  CHECK(same(v.rows[4].label, "Evolve") && same(v.rows[4].value, "not yet"));
  gStubGachi.petDex = 3;  // a final form
  gStubGachi.nutrition = gStubGachi.happiness = gStubGachi.experience = 10;
  ppMenuBuildView(st, m, PP_MENU_MAX_VISIBLE, v);
  CHECK(same(v.rows[4].label, "Retire") && same(v.rows[4].value, "READY"));
}

void checkReset() {
  const PpMenu& m = ppSettingsMenu();
  PpMenuState st = openGachi();
  nextTo(st, 8);  // Reset game
  PpMenuResult r = ppMenuInput(st, m, PP_MENU_IN_SELECT, 1000);
  CHECK(r.outcome == PP_MENU_OUT_REDRAW);
  PpMenuView v;
  ppMenuBuildView(st, m, PP_MENU_MAX_VISIBLE, v);
  CHECK(same(v.title, "RESET GAME?"));
  CHECK(v.rowCount == 3 && same(v.rows[1].label, "No, keep it") && v.rows[1].selected);
  r = ppMenuInput(st, m, PP_MENU_IN_SELECT, 1000);
  CHECK(r.outcome == PP_MENU_OUT_ACTION && r.id == PP_MENU_ACTION_GACHI_RESET_NO);
}

void renderMenus(int16_t w, int16_t h, const char* suffix) {
  auto name = [&](const char* base) { return std::string(base) + suffix; };
  const PpMenu& m = ppSettingsMenu();
  gStubGachiEnabled = true;
  ppGachiInit(gStubGachi, 5);
  gStubGachi.hasPet = true;
  gStubGachi.petDex = 4;
  gStubGachi.berries = 3;
  gStubGachi.opponentCount = 1;
  gStubGachi.dexRaised = (1u << 0) | (1u << 1) | (1u << 3);
  gStubGachi.dexShiny = (1u << 1);
  gStubGachi.sanctuaryCount = 3;
  gStubGachi.sanctuary[0] = {1, false, 3};     // oldest
  gStubGachi.sanctuary[1] = {4, true, 12};
  gStubGachi.sanctuary[2] = {7, false, 40};    // newest

  PpMenuState st = openGachi();
  drawMenuScene(w, h, st, name("menu_gachi").c_str());
  nextTo(st, 8);
  drawMenuScene(w, h, st, name("menu_gachi_last").c_str());

  // Pokedex: every visible row has its own text.
  st = openGachi();
  nextTo(st, 5);
  ppMenuInput(st, m, PP_MENU_IN_SELECT, 1000);
  PpMenuView v;
  ppMenuBuildView(st, m, PP_MENU_MAX_VISIBLE, v);
  CHECK(same(v.title, "POKEDEX"));
  CHECK(v.totalRows == 1 + PP_GACHI_SPECIES_COUNT);
  CHECK(same(v.rows[1].label, ppGachiSpeciesAt(0).name));
  CHECK(same(v.rows[2].label, (std::string(ppGachiSpeciesAt(1).name) + " *").c_str()));
  CHECK(same(v.rows[3].label, "???"));
  CHECK(same(v.rows[4].label, ppGachiSpeciesAt(3).name));
  for (uint8_t i = 1; i < v.rowCount; ++i) {
    for (uint8_t j = static_cast<uint8_t>(i + 1); j < v.rowCount; ++j) {
      if (!same(v.rows[i].label, "???") || !same(v.rows[j].label, "???")) {
        CHECK(!same(v.rows[i].label, v.rows[j].label));
        CHECK(v.rows[i].label != v.rows[j].label);  // never the same storage
      }
    }
  }
  drawMenuScene(w, h, st, name("menu_pokedex").c_str());
  nextTo(st, 11);
  gStubGachi.dexRaised = (1u << 8) | (1u << 9) | (1u << 10);  // the scrolled end shows text too
  gStubGachi.dexShiny = (1u << 10);
  drawMenuScene(w, h, st, name("menu_pokedex_end").c_str());

  // Sanctuary, newest first.
  st = openGachi();
  nextTo(st, 6);
  ppMenuInput(st, m, PP_MENU_IN_SELECT, 1000);
  ppMenuBuildView(st, m, PP_MENU_MAX_VISIBLE, v);
  CHECK(v.rowCount == 4);
  CHECK(std::string(v.rows[1].label).find(" 40W") != std::string::npos);
  CHECK(std::string(v.rows[2].label).find(" 12W *") != std::string::npos);
  CHECK(std::string(v.rows[3].label).find(" 3W") != std::string::npos);
  drawMenuScene(w, h, st, name("menu_sanctuary").c_str());

  // Reset confirm.
  st = openGachi();
  nextTo(st, 8);
  ppMenuInput(st, m, PP_MENU_IN_SELECT, 1000);
  drawMenuScene(w, h, st, name("menu_reset").c_str());

  // Off, and on with no pet.
  gStubGachiEnabled = false;
  st = openGachi();
  drawMenuScene(w, h, st, name("menu_gachi_off").c_str());
  gStubGachiEnabled = true;
  ppGachiInit(gStubGachi, 5);
  st = openGachi();
  drawMenuScene(w, h, st, name("menu_gachi_nopet").c_str());
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::printf("usage: preview <out dir>\n");
    return 2;
  }
  gOut = argv[1];
  renderHomes(240, 240, "");
  renderCards(240, 240, "");
  renderBattles(240, 240, "");
  checkGachiValues();
  checkReset();
  renderMenus(240, 240, "");
  renderHomes(172, 320, "_172x320");
  renderCards(172, 320, "_172x320");
  renderBattles(172, 320, "_172x320");
  renderMenus(172, 320, "_172x320");
  checkFrameDrawing();
  checkStagePush();
  renderStageStills();
  renderStageGifs();
  renderCardGifs();
  if (gOffStage != 0) {
    std::printf("FAIL: %d pixel(s) drawn outside the stage\n", gOffStage);
    gFailed = 1;
  }
  return gFailed;
}
