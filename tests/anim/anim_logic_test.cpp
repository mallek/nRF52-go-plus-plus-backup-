// Host tests for pp_anim_logic. Build + run from this folder (WSL or any g++):
//   g++ -std=c++17 -Wall -Wextra -Werror -I../../code/nrf_emulator/pgpemu_xiao
//       anim_logic_test.cpp ../../code/nrf_emulator/pgpemu_xiao/pp_anim_logic.cpp
//       ../../code/nrf_emulator/pgpemu_xiao/pp_feedback_logic.cpp -o t
//   ./t

#include <cstdio>
#include <cstring>

#include "pp_anim_logic.h"

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

static void testProgress()
{
  PpAnimState s = { PP_ANIM_NONE, 0 };
  CHECK(ppAnimDone(s, 0));
  ppAnimStart(s, PP_ANIM_CATCH, 100);
  CHECK(s.kind == PP_ANIM_CATCH);
  CHECK(ppAnimProgress(s, 100) == 0);
  CHECK(ppAnimProgress(s, 1100) == 500);
  CHECK(ppAnimProgress(s, 2100) == 1000);
  CHECK(ppAnimProgress(s, 9000) == 1000);
  CHECK(!ppAnimDone(s, 2099));
  CHECK(ppAnimDone(s, 2100));
}

static void testWrap()
{
  PpAnimState s = { PP_ANIM_NONE, 0 };
  ppAnimStart(s, PP_ANIM_SPIN, 0xFFFFFF00u);
  CHECK(ppAnimProgress(s, 0xFFFFFF00u + 1000u) == 500);
  CHECK(!ppAnimDone(s, 0x00000100u));
  CHECK(ppAnimDone(s, 0xFFFFFF00u + 2000u));
}

static void testRestart()
{
  PpAnimState s = { PP_ANIM_NONE, 0 };
  ppAnimStart(s, PP_ANIM_CATCH, 0);
  ppAnimStart(s, PP_ANIM_SPIN, 1500);
  CHECK(s.kind == PP_ANIM_SPIN);
  CHECK(ppAnimProgress(s, 1500) == 0);
  CHECK(!ppAnimDone(s, 3000));
}

static void testEventMap()
{
  CHECK(ppAnimKindForEvent(PP_FEEDBACK_CAUGHT) == PP_ANIM_CATCH);
  CHECK(ppAnimKindForEvent(PP_FEEDBACK_POKESTOP_SPUN) == PP_ANIM_SPIN);
  CHECK(ppAnimKindForEvent(PP_FEEDBACK_FLED) == PP_ANIM_NONE);
  CHECK(ppAnimKindForEvent(PP_FEEDBACK_CONNECTED) == PP_ANIM_NONE);
  CHECK(ppAnimKindForEvent(PP_FEEDBACK_READY) == PP_ANIM_READY);
  CHECK(ppAnimDurationMs(PP_ANIM_READY) == 2500);
  CHECK(std::strcmp(ppFeedbackEventName(PP_FEEDBACK_READY), "ready") == 0);
}

static void testEggDuration()
{
  CHECK(ppAnimDurationMs(PP_ANIM_CATCH) == 2000);
  CHECK(ppAnimDurationMs(PP_ANIM_SPIN) == 2000);
  CHECK(ppAnimDurationMs(PP_ANIM_EGG) == 2500);
  PpAnimState s = { PP_ANIM_NONE, 0 };
  ppAnimStart(s, PP_ANIM_EGG, 1000);
  CHECK(ppAnimProgress(s, 2250) == 500);
  CHECK(!ppAnimDone(s, 3000));
  CHECK(!ppAnimDone(s, 3499));
  CHECK(ppAnimDone(s, 3500));
}

static void testTapCounter()
{
  PpTapCounter t = { 0, 0 };
  CHECK(!ppTapCounterTap(t, 100));
  CHECK(!ppTapCounterTap(t, 300));
  CHECK(!ppTapCounterTap(t, 500));
  CHECK(!ppTapCounterTap(t, 700));
  CHECK(ppTapCounterTap(t, 900));    // fifth tap inside 2 s fires
  CHECK(t.count == 0);               // and resets
  // Too slow: the window restarts at the tap that fell outside it.
  CHECK(!ppTapCounterTap(t, 10000));
  CHECK(!ppTapCounterTap(t, 10500));
  CHECK(!ppTapCounterTap(t, 11000));
  CHECK(!ppTapCounterTap(t, 11500));
  CHECK(!ppTapCounterTap(t, 12100)); // 5th tap, but 2.1 s after the first
  CHECK(t.count == 1);
}

int main()
{
  testProgress();
  testWrap();
  testRestart();
  testEventMap();
  testEggDuration();
  testTapCounter();
  if (failures == 0) {
    std::printf("anim_logic: all passed\n");
    return 0;
  }
  std::printf("anim_logic: %d failure(s)\n", failures);
  return 1;
}
