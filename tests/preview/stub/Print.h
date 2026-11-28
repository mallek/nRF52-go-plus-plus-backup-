#pragma once
// Minimal Print base class for Adafruit_GFX on a PC.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* buffer, size_t size) {
    size_t n = 0;
    while (size-- > 0) {
      n += write(*buffer++);
    }
    return n;
  }
  size_t write(const char* str) {
    if (str == nullptr) {
      return 0;
    }
    return write(reinterpret_cast<const uint8_t*>(str), strlen(str));
  }
  size_t print(const char* str) { return write(str); }
  size_t print(char c) { return write(static_cast<uint8_t>(c)); }
};
