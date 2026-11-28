#include "pp_gachi_text.h"

void ppGachiResultText(PpGachiAction action, PpGachiResult result, const char*& line1, const char*& line2)
{
  line1 = "";
  line2 = "";
  switch (result) {
    case PP_GACHI_OK:
      if (action == PP_GACHI_ACT_PLAY) {
        line1 = "That was fun!";
        line2 = "Happiness up";
      } else {
        line1 = "Yum!";
        line2 = "Nutrition up";
      }
      break;
    case PP_GACHI_NO_PET:
      line1 = "No pet yet";
      line2 = "Catch a Pokemon";
      break;
    case PP_GACHI_NO_BERRIES:
      line1 = "No berries";
      line2 = "Spin a Pokestop";
      break;
    case PP_GACHI_NUTRITION_FULL:
      line1 = "It is full";
      break;
    case PP_GACHI_PLAY_NEED_FOOD:
      line1 = "It had fun";
      line2 = "Feed it first";
      break;
    case PP_GACHI_PLAY_NEED_BATTLE:
      line1 = "It had fun";
      line2 = "Win a battle first";
      break;
    case PP_GACHI_PLAY_NEED_BOTH:
      line1 = "It had fun";
      line2 = "Feed and battle";
      break;
    case PP_GACHI_PLAY_FULL:
      line1 = "It had fun";
      line2 = "Happiness is full";
      break;
    case PP_GACHI_NO_OPPONENT:
      line1 = "No one to battle";
      line2 = "Catch a Pokemon";
      break;
    case PP_GACHI_NOT_READY:
      line1 = "Not ready yet";
      break;
    case PP_GACHI_EVOLVED:
      line1 = "It evolved!";
      break;
    case PP_GACHI_RETIRED:
      line1 = "Off to the";
      line2 = "sanctuary";
      break;
    default:
      break;
  }
}
