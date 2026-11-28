#include "pp_gachi_flow.h"

// Wrap-safe: true once now has reached or passed the deadline.
static bool timePassed(uint32_t now, uint32_t deadline)
{
  return (int32_t)(uint32_t)(now - deadline) >= 0;
}

void ppGachiFlowReset(PpGachiFlow& flow)
{
  flow = PpGachiFlow();
  flow.screen = PP_GSCREEN_NONE;
  flow.card.line1 = "";
  flow.card.line2 = "";
}

void ppGachiFlowShowCard(PpGachiFlow& flow, const PpGachiCard& card, uint32_t now)
{
  flow.card = card;
  if (flow.card.line1 == nullptr) {
    flow.card.line1 = "";
  }
  if (flow.card.line2 == nullptr) {
    flow.card.line2 = "";
  }
  flow.cardUntil = now + PP_GACHI_CARD_MS;
  flow.roundShowing = false;
  flow.screen = PP_GSCREEN_CARD;
}

void ppGachiFlowStartBattle(PpGachiFlow& flow, uint8_t opponentDex)
{
  ppGachiBattleBegin(flow.battle, opponentDex);
  flow.roundShowing = false;
  flow.roundUntil = 0;
  flow.screen = PP_GSCREEN_BATTLE;
}

PpGachiFlowEvent ppGachiFlowUpdate(PpGachiFlow& flow, bool nextEdge, bool selectEdge, PpGachiPick theirPick, uint32_t now)
{
  if (flow.screen == PP_GSCREEN_CARD) {
    if (nextEdge || selectEdge || timePassed(now, flow.cardUntil)) {
      flow.screen = PP_GSCREEN_NONE;
      return PP_GFLOW_CARD_DONE;
    }
    return PP_GFLOW_NOTHING;
  }
  if (flow.screen == PP_GSCREEN_BATTLE) {
    if (flow.roundShowing) {
      // Buttons are ignored while the round shows.
      if (!timePassed(now, flow.roundUntil)) {
        return PP_GFLOW_NOTHING;
      }
      if (flow.battle.finished) {
        flow.screen = PP_GSCREEN_NONE;
        flow.roundShowing = false;
        return PP_GFLOW_BATTLE_DONE;
      }
      flow.roundShowing = false;
      return PP_GFLOW_REDRAW;
    }
    if (nextEdge && selectEdge) {
      return PP_GFLOW_NOTHING;
    }
    if (nextEdge) {
      ppGachiBattleNext(flow.battle);
      return PP_GFLOW_REDRAW;
    }
    if (selectEdge) {
      ppGachiBattleThrow(flow.battle, theirPick);
      flow.roundShowing = true;
      flow.roundUntil = now + PP_GACHI_ROUND_SHOW_MS;
      return PP_GFLOW_REDRAW;
    }
  }
  return PP_GFLOW_NOTHING;
}

void ppGachiFlowAbandon(PpGachiFlow& flow)
{
  flow.screen = PP_GSCREEN_NONE;
  flow.roundShowing = false;
}
