#include "matrix.h"

#if defined(ARDUINO)
#include <Arduino.h>

#include "config.h"
#endif

namespace bahn {
namespace {

constexpr uint8_t kRegDecode = 0x09;
constexpr uint8_t kRegIntensity = 0x0A;
constexpr uint8_t kRegScanLimit = 0x0B;
constexpr uint8_t kRegShutdown = 0x0C;
constexpr uint8_t kRegTest = 0x0F;

// Bit 7 is the left column.
constexpr uint8_t kGlyph1[8] = {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C};
constexpr uint8_t kGlyph2[8] = {0x3C, 0x66, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E};
constexpr uint8_t kGlyph3[8] = {0x3C, 0x66, 0x06, 0x1C, 0x06, 0x06, 0x66, 0x3C};

#if defined(ARDUINO)
// The common 1088AS module lights the leftmost column from bit 0.
uint8_t columnsForModule(uint8_t row) {
  row = static_cast<uint8_t>((row & 0xF0) >> 4 | (row & 0x0F) << 4);
  row = static_cast<uint8_t>((row & 0xCC) >> 2 | (row & 0x33) << 2);
  row = static_cast<uint8_t>((row & 0xAA) >> 1 | (row & 0x55) << 1);
  return row;
}
#endif

}  // namespace

void trackGlyph(uint8_t gleis, uint8_t rows[8]) {
  const uint8_t* glyph = nullptr;
  if (gleis == 1) {
    glyph = kGlyph1;
  } else if (gleis == 2) {
    glyph = kGlyph2;
  } else if (gleis == 3) {
    glyph = kGlyph3;
  }
  for (uint8_t row = 0; row < 8; ++row) {
    rows[row] = glyph == nullptr ? 0 : glyph[row];
  }
}

#if defined(ARDUINO)

void Matrix::shiftByte(uint8_t value) {
  for (int bit = 7; bit >= 0; --bit) {
    digitalWrite(kMatrixClkPin, LOW);
    digitalWrite(kMatrixDinPin, ((value >> bit) & 0x01) != 0 ? HIGH : LOW);
    digitalWrite(kMatrixClkPin, HIGH);
  }
  digitalWrite(kMatrixClkPin, LOW);
}

void Matrix::command(uint8_t reg, uint8_t ausfahrt, uint8_t einfahrt) {
  // The first 16 bits fall through to display 2. Display 1 keeps the second word.
  digitalWrite(kMatrixCsPin, LOW);
  shiftByte(reg);
  shiftByte(einfahrt);
  shiftByte(reg);
  shiftByte(ausfahrt);
  digitalWrite(kMatrixCsPin, HIGH);
  digitalWrite(kMatrixCsPin, LOW);
}

void Matrix::begin() {
  pinMode(kMatrixDinPin, OUTPUT);
  pinMode(kMatrixClkPin, OUTPUT);
  pinMode(kMatrixCsPin, OUTPUT);
  digitalWrite(kMatrixDinPin, LOW);
  digitalWrite(kMatrixClkPin, LOW);
  digitalWrite(kMatrixCsPin, LOW);

  command(kRegTest, 0, 0);
  command(kRegShutdown, 0, 0);
  command(kRegDecode, 0, 0);
  command(kRegScanLimit, 7, 7);
  command(kRegIntensity, kMatrixIntensity, kMatrixIntensity);
  for (uint8_t row = 0; row < 8; ++row) {
    command(static_cast<uint8_t>(row + 1), 0, 0);
  }
  command(kRegShutdown, 1, 1);
  ready_ = true;
  ausfahrt_ = 0xFF;
  einfahrt_ = 0xFF;
  show(0, 0);
}

void Matrix::show(uint8_t ausfahrtGleis, uint8_t einfahrtGleis) {
  if (!ready_ || (ausfahrtGleis == ausfahrt_ && einfahrtGleis == einfahrt_)) {
    return;
  }
  uint8_t ausfahrt[8];
  uint8_t einfahrt[8];
  trackGlyph(ausfahrtGleis, ausfahrt);
  trackGlyph(einfahrtGleis, einfahrt);
  for (uint8_t row = 0; row < 8; ++row) {
    command(static_cast<uint8_t>(row + 1), columnsForModule(ausfahrt[row]), columnsForModule(einfahrt[row]));
  }
  ausfahrt_ = ausfahrtGleis;
  einfahrt_ = einfahrtGleis;
}

#endif

}  // namespace bahn
