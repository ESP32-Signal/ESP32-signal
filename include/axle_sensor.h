#pragma once

#include <stdint.h>

namespace bahn {

// One wheel on the two-contact sensor.
// TowardB: contact A closes, then contact B (wheel travels toward side B).
// TowardA: contact B closes, then contact A.
enum class AxleDirection : uint8_t { None = 0, TowardA = 1, TowardB = 2 };

const char* axleDirectionName(AxleDirection direction);

// Debounced direction detector. `contactA` and `contactB` are true while the
// wheel holds that contact closed. A wheel counts once, when the second
// contact closes. The next wheel is accepted only after both contacts open.
class AxleSensor {
 public:
  AxleDirection poll(bool contactA, bool contactB, uint32_t nowMs);

 private:
  struct Level {
    bool stable = false;
    bool lastRaw = false;
    uint32_t changedMs = 0;

    void update(bool raw, uint32_t nowMs);
  };

  enum class Phase : uint8_t { Idle, FromA, FromB, Clear, Stuck };

  Level contactA_;
  Level contactB_;
  Phase phase_ = Phase::Idle;
  uint32_t markMs_ = 0;
  bool primed_ = false;
};

}  // namespace bahn
