#pragma once

#include <stdint.h>

#include "pp_gachi_battle.h"
#include "pp_gachi_tuning.h"

// PokeGachi screen flow for cards and battles. Pure logic: no Arduino, no
// allocation. The caller feeds this loop's button edges and draws on REDRAW.
// One call returns at most one event, and the call that closes a card or
// ends a battle consumes the press that came with it.

enum PpGachiScreen : uint8_t { PP_GSCREEN_NONE, PP_GSCREEN_CARD, PP_GSCREEN_BATTLE };

struct PpGachiCard {
  uint8_t dex;          // 0 = no sprite
  bool shiny;
  bool emptyBall;       // with dex 0: draw the empty ball (the no-pet card)
  const char* line1;    // never nullptr once shown
  const char* line2;
  bool isArrival;       // the caller acknowledges the arrival when this card closes
  uint8_t spriteAnim;   // a PpStageAnim value; the card's sprite plays it (Idle when 0)
  bool berry;           // draw a berry beside the sprite (the feed card)
};

struct PpGachiFlow {
  PpGachiScreen screen;
  PpGachiCard card;
  uint32_t cardUntil;
  PpGachiBattle battle;
  bool roundShowing;    // a thrown round is on screen
  uint32_t roundUntil;
};

enum PpGachiFlowEvent : uint8_t {
  PP_GFLOW_NOTHING,
  PP_GFLOW_REDRAW,
  PP_GFLOW_CARD_DONE,     // flow.card still holds the card that closed
  PP_GFLOW_BATTLE_DONE    // flow.battle.won holds the result
};

void ppGachiFlowReset(PpGachiFlow& flow);
void ppGachiFlowShowCard(PpGachiFlow& flow, const PpGachiCard& card, uint32_t now);
void ppGachiFlowStartBattle(PpGachiFlow& flow, uint8_t opponentDex);
// nextEdge and selectEdge are this loop's button press edges. theirPick is read only when a throw happens.
PpGachiFlowEvent ppGachiFlowUpdate(PpGachiFlow& flow, bool nextEdge, bool selectEdge, PpGachiPick theirPick, uint32_t now);
void ppGachiFlowAbandon(PpGachiFlow& flow);   // back to NONE with no result
