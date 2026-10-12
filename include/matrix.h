#pragma once

#include <stdint.h>

namespace bahn {

// Glyph for track 1..3, eight rows, bit 7 on the left. Track 0 and any other
// value are a dark display.
void trackGlyph(uint8_t gleis, uint8_t rows[8]);

// Daisy chain of two MAX7219 8x8 matrices. show() writes only when a digit changes.
class Matrix {
 public:
  void begin();
  void show(uint8_t ausfahrtGleis, uint8_t einfahrtGleis);

 private:
  void command(uint8_t reg, uint8_t ausfahrt, uint8_t einfahrt);
  void shiftByte(uint8_t value);
  bool ready_ = false;
  uint8_t ausfahrt_ = 0xFF;
  uint8_t einfahrt_ = 0xFF;
};

}  // namespace bahn
