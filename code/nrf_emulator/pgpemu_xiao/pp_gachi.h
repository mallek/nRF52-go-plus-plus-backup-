#pragma once

#include <stdint.h>

#include "pp_feedback_logic.h"
#include "pp_gachi_battle.h"
#include "pp_gachi_logic.h"

// PokeGachi game glue: the live state, its flash file and the catcher event intake.
// Files on InternalFS: /gachi.bin (the save) and /gachi.new (the save being written).
// Per device: an identity switch and a factory reset both keep them.
//
// Call ppGachiBegin() after ppSettingsBegin(). Everything here runs from loop(), never
// from a BLE callback: events arrive through the feedback queue.
void ppGachiBegin();                          // load or start fresh
void ppGachiOnEvent(PpFeedbackEvent event);   // from the loop() dispatch in pp_feedback.cpp
void ppGachiUpdate(uint32_t now);             // every loop(); runs the 30 minute grant save
void ppGachiFlush();                          // save now if anything is unsaved

bool ppGachiEnabled();                        // the pokegachi setting
void ppGachiSetEnabled(bool on);              // sets the setting; flushes
const PpGachiState& ppGachiState();

// With PokeGachi off these change nothing and return PP_GACHI_NO_PET.
PpGachiResult ppGachiDoFeed();                // each saves at once when it changed anything
PpGachiResult ppGachiDoPlay();
PpGachiResult ppGachiDoAdvance();
bool ppGachiBattleOpponent(uint8_t& dex);     // the oldest waiting opponent; false with no pet or none waiting
PpGachiPick ppGachiOpponentPick();            // a random pick for the opponent
void ppGachiBattleFinish(bool won);           // applies the result, saves
void ppGachiAckArrival();                     // clears arrivalPending, saves
void ppGachiResetGame();                      // fresh game, saves
