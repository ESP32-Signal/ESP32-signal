#include "axle_sensor.h"

#include "config.h"

namespace bahn {

const char* axleDirectionName(AxleDirection direction) {
  switch (direction) {
    case AxleDirection::TowardA:
      return "A";
    case AxleDirection::TowardB:
      return "B";
    case AxleDirection::None:
      break;
  }
  return "none";
}

void AxleSensor::Level::update(bool raw, uint32_t nowMs) {
  if (raw != lastRaw) {
    lastRaw = raw;
    changedMs = nowMs;
  }
  if (stable != raw && (nowMs - changedMs) >= kAxleDebounceMs) {
    stable = raw;
  }
}

AxleDirection AxleSensor::poll(bool contactA, bool contactB, uint32_t nowMs) {
  if (!primed_) {
    contactA_.stable = contactA;
    contactA_.lastRaw = contactA;
    contactA_.changedMs = nowMs;
    contactB_.stable = contactB;
    contactB_.lastRaw = contactB;
    contactB_.changedMs = nowMs;
    primed_ = true;
    if (contactA && contactB) {
      phase_ = Phase::Clear;
    } else if (contactA) {
      phase_ = Phase::FromA;
      markMs_ = nowMs;
    } else if (contactB) {
      phase_ = Phase::FromB;
      markMs_ = nowMs;
    }
    return AxleDirection::None;
  }

  contactA_.update(contactA, nowMs);
  contactB_.update(contactB, nowMs);
  const bool a = contactA_.stable;
  const bool b = contactB_.stable;

  switch (phase_) {
    case Phase::Idle:
      if (a && b) {
        phase_ = Phase::Clear;
      } else if (a) {
        phase_ = Phase::FromA;
        markMs_ = nowMs;
      } else if (b) {
        phase_ = Phase::FromB;
        markMs_ = nowMs;
      }
      break;
    case Phase::FromA:
      if (b) {
        phase_ = Phase::Clear;
        return AxleDirection::TowardB;
      }
      if (!a) {
        phase_ = Phase::Idle;
      } else if ((nowMs - markMs_) > kAxlePairWindowMs) {
        phase_ = Phase::Stuck;
      }
      break;
    case Phase::FromB:
      if (a) {
        phase_ = Phase::Clear;
        return AxleDirection::TowardA;
      }
      if (!b) {
        phase_ = Phase::Idle;
      } else if ((nowMs - markMs_) > kAxlePairWindowMs) {
        phase_ = Phase::Stuck;
      }
      break;
    case Phase::Clear:
    case Phase::Stuck:
      if (!a && !b) {
        phase_ = Phase::Idle;
      }
      break;
  }
  return AxleDirection::None;
}

}  // namespace bahn
