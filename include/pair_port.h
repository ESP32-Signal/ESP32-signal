#pragma once

#include "protocol.h"

namespace bahn {

// One pairing socket. The cable is the only trigger: an unpaired port sends
// Hello until the module on the other end answers. A Hello or Ack from a new
// MAC replaces the stored neighbor.
struct PairRxResult {
  bool peerChanged = false;
  bool sendAck = false;
};

class PairPort {
 public:
  void init(const uint8_t localMac[kMacSize], Side localSide, ModuleRole localRole);
  void forget();
  void restore(const uint8_t mac[kMacSize], ModuleRole role, Side remoteSide);

  bool paired() const { return paired_; }
  const uint8_t* peerMac() const { return peer_; }
  ModuleRole peerRole() const { return peerRole_; }
  Side peerSide() const { return peerSide_; }
  Side localSide() const { return localSide_; }

  // True when an unpaired port should transmit Hello. `nowMs` is monotonic.
  bool helloDue(uint32_t nowMs, Frame& out);

  PairRxResult onFrame(const Frame& rx);
  void fillAck(Frame& out) const;

 private:
  void fillHello(Frame& out) const;

  uint8_t local_[kMacSize] = {};
  uint8_t peer_[kMacSize] = {};
  Side localSide_ = Side::A;
  Side peerSide_ = Side::A;
  ModuleRole localRole_ = ModuleRole::Block;
  ModuleRole peerRole_ = ModuleRole::Unknown;
  bool paired_ = false;
  bool helloStarted_ = false;
  uint32_t lastHelloMs_ = 0;
};

}  // namespace bahn
