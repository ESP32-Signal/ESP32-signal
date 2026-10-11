#pragma once

#include "axle_sensor.h"
#include "protocol.h"

namespace bahn {

// Axle counter for one block boundary.
//
// The sensor sits between the block toward side A and the block toward side B.
// A wheel toward B enters the block facing B and leaves the block facing A.
// The net count is positive for axles still in the block toward B, negative
// for axles still in the block toward A, and zero when this module's entry
// count has been released by Rückblock or the train rolled back out.
//
// The first axle of an entry sends Vorblock at once. Later axles update
// Achszahl. The module that watches the train leave sends Rückblock once its
// own passages equal the neighbor's Achszahl and both counts have settled.
// Receiving that Rückblock sets this counter back to zero.
class AxleCounter {
 public:
  void onPassage(AxleDirection direction, uint32_t nowMs);
  void onRemote(Side from, MsgType type, uint16_t achszahl, uint32_t nowMs);
  void update(uint32_t nowMs);
  void onSendFailed(Side side, MsgType type);

  bool takeVorblock(Side side, uint16_t& achszahl);
  bool takeAchszahl(Side side, uint16_t& achszahl);
  bool takeRueckblock(Side side, uint16_t& achszahl);

  int16_t net() const { return net_; }
  bool occupied(Side side) const;
  uint16_t countToward(Side side) const;
  bool approachFrom(Side side) const;
  bool neighborExpected(Side side, uint16_t& achszahl) const;

 private:
  struct Outgoing {
    bool vorblock = false;
    bool achszahl = false;
    bool rueckblock = false;
    uint16_t count = 0;
    uint16_t rueckCount = 0;
  };

  void publishEntry(Side side, uint16_t count);
  void publishClear(Side side);
  void noteRemoteCount(Side from, uint16_t count, uint32_t nowMs);
  void evaluate(uint32_t nowMs);
  void tryRelease(uint32_t nowMs);
  bool settled(uint32_t stampMs, bool valid, uint32_t nowMs) const;

  int16_t net_ = 0;
  bool moved_ = false;
  uint32_t lastChangeMs_ = 0;
  bool announced_[2] = {};
  bool approach_[2] = {};
  bool expectValid_[2] = {};
  bool rueckSent_[2] = {};
  uint16_t expected_[2] = {};
  uint32_t expectedMs_[2] = {};
  bool releasePending_[2] = {};
  uint16_t releaseCount_[2] = {};
  Outgoing tx_[2];
};

}  // namespace bahn
