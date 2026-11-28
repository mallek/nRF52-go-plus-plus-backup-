// Host tests for pp_gachi_text. Build + run from this folder (WSL or any g++):
//   g++ -std=c++17 -Wall -Wextra -Werror -I../../code/nrf_emulator/pgpemu_xiao
//       gachi_text_test.cpp ../../code/nrf_emulator/pgpemu_xiao/pp_gachi_text.cpp -o t
//   ./t
// Or all host tests: bash tests/run.sh

#include <cstdio>
#include <cstring>

#include "pp_gachi_text.h"

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

static bool is(PpGachiAction a, PpGachiResult r, const char* e1, const char* e2)
{
  const char* l1 = nullptr;
  const char* l2 = nullptr;
  ppGachiResultText(a, r, l1, l2);
  return l1 != nullptr && l2 != nullptr && std::strcmp(l1, e1) == 0 && std::strcmp(l2, e2) == 0;
}

static void testTable()
{
  CHECK(is(PP_GACHI_ACT_FEED, PP_GACHI_OK, "Yum!", "Nutrition up"));
  CHECK(is(PP_GACHI_ACT_FEED, PP_GACHI_NO_BERRIES, "No berries", "Spin a Pokestop"));
  CHECK(is(PP_GACHI_ACT_FEED, PP_GACHI_NUTRITION_FULL, "It is full", ""));
  CHECK(is(PP_GACHI_ACT_PLAY, PP_GACHI_OK, "That was fun!", "Happiness up"));
  CHECK(is(PP_GACHI_ACT_PLAY, PP_GACHI_PLAY_NEED_FOOD, "It had fun", "Feed it first"));
  CHECK(is(PP_GACHI_ACT_PLAY, PP_GACHI_PLAY_NEED_BATTLE, "It had fun", "Win a battle first"));
  CHECK(is(PP_GACHI_ACT_PLAY, PP_GACHI_PLAY_NEED_BOTH, "It had fun", "Feed and battle"));
  CHECK(is(PP_GACHI_ACT_PLAY, PP_GACHI_PLAY_FULL, "It had fun", "Happiness is full"));
  CHECK(is(PP_GACHI_ACT_BATTLE, PP_GACHI_NO_OPPONENT, "No one to battle", "Catch a Pokemon"));
  CHECK(is(PP_GACHI_ACT_ADVANCE, PP_GACHI_NOT_READY, "Not ready yet", ""));
  CHECK(is(PP_GACHI_ACT_ADVANCE, PP_GACHI_EVOLVED, "It evolved!", ""));
  CHECK(is(PP_GACHI_ACT_ADVANCE, PP_GACHI_RETIRED, "Off to the", "sanctuary"));
  for (int a = PP_GACHI_ACT_FEED; a <= PP_GACHI_ACT_ADVANCE; ++a) {
    CHECK(is((PpGachiAction)a, PP_GACHI_NO_PET, "No pet yet", "Catch a Pokemon"));
  }
}

static void testAllPairsWellFormed()
{
  for (int a = PP_GACHI_ACT_FEED; a <= PP_GACHI_ACT_ADVANCE; ++a) {
    for (int r = PP_GACHI_OK; r <= PP_GACHI_RETIRED; ++r) {
      const char* l1 = nullptr;
      const char* l2 = nullptr;
      ppGachiResultText((PpGachiAction)a, (PpGachiResult)r, l1, l2);
      CHECK(l1 != nullptr);
      CHECK(l2 != nullptr);
      if (l1 == nullptr || l2 == nullptr) {
        continue;
      }
      CHECK(std::strlen(l1) <= 18);
      CHECK(std::strlen(l2) <= 18);
      for (const char* p = l1; *p != '\0'; ++p) {
        CHECK(*p >= 0x20 && *p <= 0x7E);
      }
      for (const char* p = l2; *p != '\0'; ++p) {
        CHECK(*p >= 0x20 && *p <= 0x7E);
      }
    }
  }
}

static void testOutOfRange()
{
  const char* l1 = nullptr;
  const char* l2 = nullptr;
  ppGachiResultText((PpGachiAction)99, (PpGachiResult)99, l1, l2);
  CHECK(l1 != nullptr && l2 != nullptr);
}

static void testRetiredDiffersFromEvolved()
{
  const char* e1 = nullptr;
  const char* e2 = nullptr;
  const char* r1 = nullptr;
  const char* r2 = nullptr;
  ppGachiResultText(PP_GACHI_ACT_ADVANCE, PP_GACHI_EVOLVED, e1, e2);
  ppGachiResultText(PP_GACHI_ACT_ADVANCE, PP_GACHI_RETIRED, r1, r2);
  CHECK(std::strcmp(e1, r1) != 0);
}

int main()
{
  testTable();
  testAllPairsWellFormed();
  testOutOfRange();
  testRetiredDiffersFromEvolved();
  if (failures == 0) {
    std::printf("gachi_text: all passed\n");
    return 0;
  }
  std::printf("gachi_text: %d failure(s)\n", failures);
  return 1;
}
