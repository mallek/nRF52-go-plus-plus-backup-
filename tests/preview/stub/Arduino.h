#pragma once
// Minimum Arduino surface needed to compile Adafruit_GFX and pp_board.h on a PC.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef optimistic_yield
#define optimistic_yield(x)
#endif

#include <math.h>
#include <string>

#ifndef radians
#define radians(deg) ((deg) * 0.017453292519943295)
#endif

class __FlashStringHelper;
// Only the part of String that Adafruit_GFX::getTextBounds touches.
class String : public std::string {
 public:
  String(const char* s = "") : std::string(s) {}
  unsigned int length() const { return static_cast<unsigned int>(size()); }
};

typedef bool boolean;
typedef uint8_t byte;

// pp_board.h only names this inside an inline helper the preview never calls.
static const uint32_t g_ADigitalPinMap[64] = {0};

#include "Print.h"
