#pragma once

#include "pp_menu_logic.h"

// The settings menu: its pages, rows and where each value comes from. This is
// the one place to edit when a setting is added or removed. No drawing and no
// button code here.
//
// To add an on/off or multi-choice setting:
//   1. add the field and its setter (pp_settings_logic, pp_settings)
//   2. add one entry to kChoices in pp_menu_items.cpp
//   3. add one row to kTopItems there
// The menu logic and the drawing do not change.

// ACTION ids the display code has to carry out.
static constexpr uint8_t PP_MENU_ACTION_IDENTITY = 1;  // arg = identity index

static constexpr uint8_t PP_MENU_ACTION_GACHI_FEED = 10;
static constexpr uint8_t PP_MENU_ACTION_GACHI_PLAY = 11;
static constexpr uint8_t PP_MENU_ACTION_GACHI_BATTLE = 12;
static constexpr uint8_t PP_MENU_ACTION_GACHI_ADVANCE = 13;
static constexpr uint8_t PP_MENU_ACTION_GACHI_RESET_NO = 14;
static constexpr uint8_t PP_MENU_ACTION_GACHI_RESET_YES = 15;
// Selecting a row of these lists is reported as an action with this id; arg = row.
static constexpr uint8_t PP_MENU_LIST_POKEDEX = 20;
static constexpr uint8_t PP_MENU_LIST_SANCTUARY = 21;

const PpMenu& ppSettingsMenu();
uint8_t ppMenuPagePokegachi();  // page index, for ppMenuOpenAt
