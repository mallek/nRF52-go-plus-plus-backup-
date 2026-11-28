#include "pp_gachi_battle.h"

PpGachiRound ppGachiRoundOutcome(PpGachiPick mine, PpGachiPick theirs)
{
  if (mine == theirs) {
    return PP_ROUND_TIE;
  }
  // Each pick beats the one after it in the order grass, water, fire, grass.
  bool win = (mine == PP_PICK_FIRE && theirs == PP_PICK_GRASS) ||
             (mine == PP_PICK_GRASS && theirs == PP_PICK_WATER) ||
             (mine == PP_PICK_WATER && theirs == PP_PICK_FIRE);
  return win ? PP_ROUND_WIN : PP_ROUND_LOSE;
}

const char* ppGachiPickName(PpGachiPick pick)
{
  switch (pick) {
    case PP_PICK_WATER:
      return "WATER";
    case PP_PICK_GRASS:
      return "GRASS";
    default:
      return "FIRE";
  }
}

void ppGachiBattleBegin(PpGachiBattle& b, uint8_t opponentDex)
{
  b = PpGachiBattle();
  b.active = true;
  b.opponentDex = opponentDex;
  b.cursor = PP_PICK_FIRE;
  b.myPick = PP_PICK_FIRE;
  b.theirPick = PP_PICK_FIRE;
  b.lastRound = PP_ROUND_NONE;
}

void ppGachiBattleNext(PpGachiBattle& b)
{
  if (!b.active || b.finished) {
    return;
  }
  b.cursor = (uint8_t)((b.cursor + 1) % PP_PICK_COUNT);
}

void ppGachiBattleThrow(PpGachiBattle& b, PpGachiPick theirPick)
{
  if (!b.active || b.finished) {
    return;
  }
  if ((uint8_t)theirPick >= PP_PICK_COUNT) {
    theirPick = PP_PICK_FIRE;
  }
  b.myPick = (b.cursor < PP_PICK_COUNT) ? (PpGachiPick)b.cursor : PP_PICK_FIRE;
  b.theirPick = theirPick;
  b.lastRound = ppGachiRoundOutcome(b.myPick, b.theirPick);
  if (b.lastRound == PP_ROUND_WIN) {
    ++b.myWins;
  } else if (b.lastRound == PP_ROUND_LOSE) {
    ++b.theirWins;
  }
  if (b.myWins >= PP_GACHI_ROUND_WINS) {
    b.finished = true;
    b.won = true;
  } else if (b.theirWins >= PP_GACHI_ROUND_WINS) {
    b.finished = true;
    b.won = false;
  }
}
