// PC preview of the PokeGachi drawing code. Renders the real pp_gachi_ui code
// into an in-memory RGB565 canvas and writes one PPM file per scene.
// Built and run by render.sh; see there for the usage.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Arduino.h"
#include "Adafruit_GFX.h"
#include "pp_gachi_battle.h"
#include "pp_gachi_flow.h"
#include "pp_gachi_logic.h"
#include "pp_gachi_species.h"
#include "pp_gachi_text.h"
#include "pp_gachi_ui.h"

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

void renderHomes(int16_t w, int16_t h, const char* suffix) {
  auto name = [&](const char* base) { return std::string(base) + suffix; };
  {
    Canvas c(w, h);
    PpGachiState st = basePet(4, false);
    st.nutrition = 4;
    st.happiness = 2;
    st.experience = 3;
    ppGachiDrawHome(c, w, h, st, normalLines(), kColors);
    save(c, name("home_pet").c_str());
  }
  {
    Canvas c(w, h);
    PpGachiState st = basePet(2, false);
    st.nutrition = 10;
    st.happiness = 10;
    st.experience = 10;
    ppGachiDrawHome(c, w, h, st, normalLines(), kColors);
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
    ppGachiDrawHome(c, w, h, st, normalLines(), kColors);
    save(c, name("home_shiny_10").c_str());
  }
  {
    Canvas c(w, h);
    PpGachiState st;
    ppGachiInit(st, 99);
    ppGachiDrawHome(c, w, h, st, normalLines(), kColors);
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
    ppGachiDrawHome(c, w, h, st, l, kColors);
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
    ppGachiDrawHome(c, w, h, st, l, kColors);
    save(c, name("home_countdown").c_str());
  }
}

void renderCards(int16_t w, int16_t h, const char* suffix) {
  auto name = [&](const char* base) { return std::string(base) + suffix; };
  {
    Canvas c(w, h);
    PpGachiCard card = {};
    card.dex = 4;
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
  renderHomes(172, 320, "_172x320");
  renderCards(172, 320, "_172x320");
  renderBattles(172, 320, "_172x320");
  return gFailed;
}
