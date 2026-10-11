#include "axle_counter.h"

#include "config.h"

namespace bahn {
namespace {

size_t indexOf(Side side) { return static_cast<size_t>(side); }

}  // namespace

void AxleCounter::onPassage(AxleDirection direction, uint32_t nowMs) {
  if (direction != AxleDirection::TowardA && direction != AxleDirection::TowardB) {
    return;
  }
  const int16_t before = net_;
  int32_t next = static_cast<int32_t>(net_) + (direction == AxleDirection::TowardB ? 1 : -1);
  if (next > 32767) {
    next = 32767;
  }
  if (next < -32767) {
    next = -32767;
  }
  if (next == before) {
    return;
  }

  net_ = static_cast<int16_t>(next);
  lastChangeMs_ = nowMs;
  moved_ = true;

  if (net_ > 0) {
    publishEntry(Side::B, static_cast<uint16_t>(net_));
  } else if (net_ < 0) {
    publishEntry(Side::A, static_cast<uint16_t>(-net_));
  } else if (before > 0) {
    publishClear(Side::B);
  } else if (before < 0) {
    publishClear(Side::A);
  }
  evaluate(nowMs);
  tryRelease(nowMs);
}

void AxleCounter::onRemote(Side from, MsgType type, uint16_t achszahl, uint32_t nowMs) {
  if (!validSide(from)) {
    return;
  }
  if (type == MsgType::Vorblock) {
    // A Vorblock opens a new occupation even when the axle count is unchanged.
    approach_[indexOf(from)] = true;
    rueckSent_[indexOf(from)] = false;
    expectedMs_[indexOf(from)] = nowMs;
    if (achszahl > 0) {
      noteRemoteCount(from, achszahl, nowMs);
    }
  } else if (type == MsgType::Achszahl) {
    noteRemoteCount(from, achszahl, nowMs);
    if (achszahl == 0) {
      approach_[indexOf(from)] = false;
    }
  } else if (type == MsgType::Rueckblock) {
    releasePending_[indexOf(from)] = true;
    releaseCount_[indexOf(from)] = achszahl;
    tryRelease(nowMs);
  }
  evaluate(nowMs);
}

void AxleCounter::update(uint32_t nowMs) {
  evaluate(nowMs);
  tryRelease(nowMs);
}

void AxleCounter::onSendFailed(Side side, MsgType type) {
  if (!validSide(side)) {
    return;
  }
  Outgoing& tx = tx_[indexOf(side)];
  if (type == MsgType::Vorblock) {
    if (occupied(side)) {
      tx.vorblock = true;
      tx.count = countToward(side);
    }
    return;
  }
  if (type == MsgType::Achszahl) {
    tx.achszahl = true;
    tx.count = occupied(side) ? countToward(side) : 0;
    return;
  }
  if (type == MsgType::Rueckblock && rueckSent_[indexOf(side)]) {
    tx.rueckblock = true;
  }
}

bool AxleCounter::takeVorblock(Side side, uint16_t& achszahl) {
  if (!validSide(side) || !tx_[indexOf(side)].vorblock) {
    return false;
  }
  achszahl = tx_[indexOf(side)].count;
  tx_[indexOf(side)].vorblock = false;
  return true;
}

bool AxleCounter::takeAchszahl(Side side, uint16_t& achszahl) {
  if (!validSide(side) || !tx_[indexOf(side)].achszahl) {
    return false;
  }
  achszahl = tx_[indexOf(side)].count;
  tx_[indexOf(side)].achszahl = false;
  return true;
}

bool AxleCounter::takeRueckblock(Side side, uint16_t& achszahl) {
  if (!validSide(side) || !tx_[indexOf(side)].rueckblock) {
    return false;
  }
  achszahl = tx_[indexOf(side)].rueckCount;
  tx_[indexOf(side)].rueckblock = false;
  return true;
}

bool AxleCounter::occupied(Side side) const {
  if (side == Side::B) {
    return net_ > 0;
  }
  if (side == Side::A) {
    return net_ < 0;
  }
  return false;
}

uint16_t AxleCounter::countToward(Side side) const {
  if (side == Side::B && net_ > 0) {
    return static_cast<uint16_t>(net_);
  }
  if (side == Side::A && net_ < 0) {
    return static_cast<uint16_t>(-net_);
  }
  return 0;
}

bool AxleCounter::approachFrom(Side side) const {
  return validSide(side) && approach_[indexOf(side)];
}

bool AxleCounter::neighborExpected(Side side, uint16_t& achszahl) const {
  if (!validSide(side) || !expectValid_[indexOf(side)]) {
    return false;
  }
  achszahl = expected_[indexOf(side)];
  return true;
}

void AxleCounter::publishEntry(Side side, uint16_t count) {
  Outgoing& tx = tx_[indexOf(side)];
  if (!announced_[indexOf(side)]) {
    announced_[indexOf(side)] = true;
    tx.vorblock = true;
  }
  tx.achszahl = true;
  tx.count = count;
}

void AxleCounter::publishClear(Side side) {
  Outgoing& tx = tx_[indexOf(side)];
  announced_[indexOf(side)] = false;
  tx.vorblock = false;
  tx.achszahl = true;
  tx.count = 0;
  const Side origin = side == Side::B ? Side::A : Side::B;
  rueckSent_[indexOf(origin)] = false;
}

void AxleCounter::noteRemoteCount(Side from, uint16_t count, uint32_t nowMs) {
  const size_t index = indexOf(from);
  const bool changed = !expectValid_[index] || expected_[index] != count;
  if (changed) {
    rueckSent_[index] = false;
    expectedMs_[index] = nowMs;
  }
  expected_[index] = count;
  expectValid_[index] = count > 0;
  if (count == 0) {
    rueckSent_[index] = false;
    approach_[index] = false;
  }
}

void AxleCounter::evaluate(uint32_t nowMs) {
  if (!settled(lastChangeMs_, moved_, nowMs)) {
    return;
  }

  if (net_ > 0 && expectValid_[indexOf(Side::A)] && expected_[indexOf(Side::A)] == static_cast<uint16_t>(net_) &&
      settled(expectedMs_[indexOf(Side::A)], true, nowMs) && !rueckSent_[indexOf(Side::A)]) {
    tx_[indexOf(Side::A)].rueckblock = true;
    tx_[indexOf(Side::A)].rueckCount = static_cast<uint16_t>(net_);
    rueckSent_[indexOf(Side::A)] = true;
    approach_[indexOf(Side::A)] = false;
  }

  if (net_ < 0 && expectValid_[indexOf(Side::B)] && expected_[indexOf(Side::B)] == static_cast<uint16_t>(-net_) &&
      settled(expectedMs_[indexOf(Side::B)], true, nowMs) && !rueckSent_[indexOf(Side::B)]) {
    tx_[indexOf(Side::B)].rueckblock = true;
    tx_[indexOf(Side::B)].rueckCount = static_cast<uint16_t>(-net_);
    rueckSent_[indexOf(Side::B)] = true;
    approach_[indexOf(Side::B)] = false;
  }
}

void AxleCounter::tryRelease(uint32_t nowMs) {
  if (!settled(lastChangeMs_, moved_, nowMs)) {
    return;
  }
  for (size_t i = 0; i < 2; ++i) {
    if (!releasePending_[i]) {
      continue;
    }
    const Side side = static_cast<Side>(i);
    const uint16_t mine = countToward(side);
    if (mine > 0 && mine == releaseCount_[i]) {
      net_ = 0;
      announced_[i] = false;
      tx_[i].vorblock = false;
      tx_[i].achszahl = false;
      rueckSent_[0] = false;
      rueckSent_[1] = false;
      releasePending_[0] = false;
      releasePending_[1] = false;
      return;
    }
    releasePending_[i] = false;
  }
}

bool AxleCounter::settled(uint32_t stampMs, bool valid, uint32_t nowMs) const {
  return valid && (nowMs - stampMs) >= kAxleSettleMs;
}

}  // namespace bahn
