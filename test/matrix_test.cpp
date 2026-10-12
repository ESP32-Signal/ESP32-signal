#include "matrix.h"

#include <stdio.h>
#include <string.h>

namespace {

int failures = 0;

void expect(bool cond, const char* name) {
  if (cond) {
    return;
  }
  fprintf(stderr, "FAIL %s\n", name);
  failures++;
}

bool blank(const uint8_t rows[8]) {
  for (uint8_t i = 0; i < 8; ++i) {
    if (rows[i] != 0) {
      return false;
    }
  }
  return true;
}

void testGlyphs() {
  uint8_t dark[8];
  uint8_t one[8];
  uint8_t two[8];
  uint8_t three[8];
  uint8_t other[8];
  bahn::trackGlyph(0, dark);
  bahn::trackGlyph(1, one);
  bahn::trackGlyph(2, two);
  bahn::trackGlyph(3, three);
  bahn::trackGlyph(4, other);
  expect(blank(dark), "no track is dark");
  expect(blank(other), "unknown track is dark");
  expect(!blank(one) && !blank(two) && !blank(three), "digits 1 to 3 are lit");
  expect(memcmp(one, two, 8) != 0 && memcmp(two, three, 8) != 0 && memcmp(one, three, 8) != 0,
         "the three digits differ");
  expect(one[0] == 0x18 && two[0] == 0x3C && three[0] == 0x3C, "digit shapes");
}

}  // namespace

int main() {
  testGlyphs();
  if (failures != 0) {
    fprintf(stderr, "%d failed\n", failures);
    return 1;
  }
  printf("ok\n");
  return 0;
}
