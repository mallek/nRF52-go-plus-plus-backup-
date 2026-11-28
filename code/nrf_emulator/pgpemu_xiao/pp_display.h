#pragma once

#include <stdint.h>

#include "pp_feedback_logic.h"

// Screen + settings menu (CLUE). On boards without a display every call is a no-op
// and ppUiOwnsButtons() is always false.
//
// CLUE controls:
//   hold A 5 s     factory reset (handled by the lifecycle code, not here)
//   hold A+B 1.5 s open the settings menu; B = next, A = select. Back is the
//                  first row of every list. It closes after 20 s without a press.
//   A or B         wake the screen for 30 s (5 s while disconnected)
// A catch, flee or spin does not light a dark screen (battery); it only counts.
// The animations play when the screen is already lit.
//
// PokeGachi (menu: PokeGachi > Enabled, OFF by default). Off, the home screen is
// the text screen above and nothing below applies. On:
//   the home screen is the pet face: identity and link word on top, the pet's
//   sprite (an empty ball before a pet), name, stage and the three tracks, the
//   berries and opponents (the sleep countdown takes that line while it runs),
//   then the last event text or the counters. The reset bar draws over the
//   lines under the sprite.
//   B x5 in 2 s    open the PokeGachi page: Feed, Play, Battle, Evolve (Retire
//                  for a final form), Pokedex, Sanctuary, Enabled, Reset game.
//                  The same page is the PokeGachi row of the settings menu.
//   Feed, Play, Evolve and a battle's end show a result card for about 2 s or
//   until a press, then go back to the page. Play first shows the pet chasing
//   something about 5 s and Evolve a flicker between the two forms; any button skips
//   either one and goes to the card. A pet that arrives is announced
//   by a card on the next lit home screen.
//   battle         B = next pick, A = throw; first to two round wins. The menu
//                  timeout waits; the screen sleep abandons the battle with no
//                  result.
void ppDisplayBegin();
void ppDisplayUpdate();
void ppDisplayOnEvent(PpFeedbackEvent event);
void ppDisplayOnFactoryReset();
// The disconnected System OFF timer, so the screen can show a countdown.
void ppDisplaySetSleepDeadline(bool armed, uint32_t atMs);
void ppDisplayShutdown();

// True while the menu, a PokeGachi card, battle, play walk or evolve flicker, or
// a two-button gesture is using the buttons, so the hold-A factory reset must not fire.
bool ppUiOwnsButtons();
