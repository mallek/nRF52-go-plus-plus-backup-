#pragma once
// Pure state machine for the pet stage on the CLUE: what the pet is doing at a given time.
// No drawing, no clock, no allocation. The caller passes `now` in milliseconds.

#include <stdint.h>

#include "pp_anim_logic.h"

enum PpStageBase : uint8_t { PP_STAGE_EMPTY, PP_STAGE_IDLE, PP_STAGE_ASLEEP };
enum PpStageAnim : uint8_t { PP_SANIM_IDLE, PP_SANIM_HOP, PP_SANIM_SLEEP, PP_SANIM_EAT, PP_SANIM_WALK, PP_SANIM_COUNT };
enum PpStageScene : uint8_t {
  PP_SCENE_MEADOW, PP_SCENE_BEACH, PP_SCENE_FOREST, PP_SCENE_CAMP_NIGHT,   // the four play scenes
  PP_SCENE_MEADOW_NIGHT,                                                  // home while not connected
  PP_SCENE_COUNT
};
static constexpr uint8_t PP_SCENE_PLAY_COUNT = 4;
enum PpStageSymbol : uint8_t { PP_SYM_NONE, PP_SYM_HEARTS, PP_SYM_STARS, PP_SYM_BERRY, PP_SYM_BERRY_BOUNCE, PP_SYM_ALERT, PP_SYM_ZZZ };
enum PpStageMoment : uint8_t { PP_MOMENT_NONE, PP_MOMENT_PLAY, PP_MOMENT_EVOLVE };

static constexpr uint32_t PP_STAGE_PLAY_MS = 5000;
static constexpr uint32_t PP_STAGE_EVOLVE_MS = 2500;
static constexpr int16_t PP_STAGE_WALK_SPAN = 60;   // how far left and right of centre the walk goes, on a 240 wide stage

struct PpAnimTiming { uint8_t frames; const uint16_t* ms; };   // frames == 0: this pet has no such animation
struct PpStageTimings { PpAnimTiming anim[PP_SANIM_COUNT]; };

struct PpStagePose {
  PpStageScene scene;
  bool emptyBall;       // draw the empty ball, no pet
  bool oldForm;         // evolve flicker: draw the form the pet is leaving
  PpStageAnim anim;     // already resolved: never an animation with 0 frames unless Idle itself has none
  uint8_t frame;
  int16_t dx, dy;       // offset from the centre of the ground line, 240-wide units; dy negative is up
  bool mirror;          // drawn facing the other way
  bool tipped;          // lying on its side (Flop)
  PpStageSymbol symbol;
  uint16_t symbolProgress;   // 0..1000 through the symbol's own animation
};

struct PpStage {
  PpStageBase base;
  uint32_t baseAt;
  bool animationsOn;
  PpPetReaction reaction;
  uint32_t reactionAt;
  PpStageMoment moment;
  uint32_t momentAt;
  PpStageScene momentScene;
  bool hasLast;
  PpStagePose last;
};

void ppStageInit(PpStage& st, uint32_t now);                    // EMPTY, animations on, nothing playing
void ppStageSetBase(PpStage& st, PpStageBase base, uint32_t now);   // no-op when unchanged; a change drops any reaction
void ppStageSetAnimations(PpStage& st, bool on);                // turning off drops any reaction and moment
void ppStageReact(PpStage& st, PpPetReaction reaction, uint32_t now);
void ppStageStartPlay(PpStage& st, PpStageScene scene, uint32_t now);
void ppStageStartEvolve(PpStage& st, uint32_t now);
void ppStageCancel(PpStage& st);                                // drops the reaction and the moment
uint32_t ppStageReactionMs(PpPetReaction reaction);             // lengths from Global Constraints; NONE is 0
bool ppStageBusy(const PpStage& st, uint32_t now);              // a reaction or moment is still playing
bool ppStageMomentDone(const PpStage& st, uint32_t now);        // a moment was started and its time has passed
// The pose at `now`. Returns true when it differs from the pose returned by the previous call
// (always true on the first call after Init, SetBase, SetAnimations, React, Start or Cancel).
bool ppStagePose(PpStage& st, const PpStageTimings& timings, uint32_t now, PpStagePose& out);

// The frame of a looping animation `elapsed` ms after it began (a card's sprite uses it).
// 0 when the animation has no frames or no usable times.
uint8_t ppStageLoopFrame(const PpAnimTiming& t, uint32_t elapsed);
// One of the four play scenes for a seed (the display passes micros()). The seed is mixed,
// so nearby seeds spread over all four. A pure function: never the game's random state.
PpStageScene ppStagePlayScene(uint32_t seed);

// The base state the catcher calls for: EMPTY with no pet, else IDLE while GO is ready, else ASLEEP.
PpStageBase ppStageBaseFor(bool hasPet, bool goReady);
// Whether GO is ready after an event: READY sets it, DISCONNECTED clears it, anything else
// (the raw CONNECTED too) leaves it as it was.
bool ppStageGoReadyAfter(bool goReady, PpFeedbackEvent event);
