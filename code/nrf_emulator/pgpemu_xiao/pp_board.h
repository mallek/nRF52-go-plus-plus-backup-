#pragma once

#include <Arduino.h>

// Board differences in one place. The protocol code is board-agnostic; only the
// reset input, the feedback hardware and the battery gauge change.

#if defined(ARDUINO_NRF52840_CLUE)

#define PP_BOARD_CLUE 1
#define PP_BOARD_NAME "CLUE"
// Button A is the factory reset (hold 5 s) and the System OFF wake source.
#define PP_RESET_BUTTON_ARDUINO_PIN PIN_BUTTON1
#define PP_SECOND_BUTTON_ARDUINO_PIN PIN_BUTTON2
#define PP_HAS_DISPLAY 1
#define PP_HAS_BATTERY_GAUGE 0
#define PP_LED_PIN PIN_NEOPIXEL
#define PP_LED_PHYSICAL_COUNT 1

#else

#define PP_BOARD_CLUE 0
#define PP_BOARD_NAME "XIAO"
// XIAO nRF52840: P0.17 is the charger CHG line, so the reset is D1 (P0.03) to GND.
#define PP_RESET_BUTTON_ARDUINO_PIN D1
#define PP_HAS_DISPLAY 0
#define PP_HAS_BATTERY_GAUGE 1
#define PP_LED_PIN D7
#define PP_LED_PHYSICAL_COUNT 7

#endif

// Raw nRF pin number (P0.xx = xx, P1.xx = 32 + xx) for the nrf_gpio_* calls.
inline uint32_t ppRawPin(uint32_t arduinoPin)
{
  return g_ADigitalPinMap[arduinoPin];
}
