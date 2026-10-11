#include "pair_port.h"

#include "config.h"

#include <string.h>

namespace bahn {

void PairPort::init(const uint8_t localMac[kMacSize], Side localSide, ModuleRole localRole) {
  macCopy(local_, localMac);
  localSide_ = localSide;
  localRole_ = localRole;
  forget();
}

void PairPort::forget() {
  memset(peer_, 0, kMacSize);
  peerRole_ = ModuleRole::Unknown;
  peerSide_ = Side::A;
  paired_ = false;
  helloStarted_ = false;
  lastHelloMs_ = 0;
}

void PairPort::restore(const uint8_t mac[kMacSize], ModuleRole role, Side remoteSide) {
  if (!macValid(mac) || macEqual(mac, local_)) {
    return;
  }
  macCopy(peer_, mac);
  peerRole_ = role;
  peerSide_ = remoteSide;
  paired_ = true;
  helloStarted_ = false;
}

bool PairPort::helloDue(uint32_t nowMs, Frame& out) {
  if (paired_) {
    return false;
  }
  if (helloStarted_ && (nowMs - lastHelloMs_) < kPairHelloIntervalMs) {
    return false;
  }
  helloStarted_ = true;
  lastHelloMs_ = nowMs;
  fillHello(out);
  return true;
}

PairRxResult PairPort::onFrame(const Frame& rx) {
  PairRxResult result;
  const uint8_t hello = static_cast<uint8_t>(PairType::Hello);
  const uint8_t ack = static_cast<uint8_t>(PairType::Ack);
  if (rx.type != hello && rx.type != ack) {
    return result;
  }

  uint8_t mac[kMacSize];
  Side remote = Side::A;
  ModuleRole role = ModuleRole::Unknown;
  if (!unpackPairIdentity(rx, mac, remote, role) || macEqual(mac, local_)) {
    return result;
  }

  if (rx.type == hello) {
    result.sendAck = true;
  }
  result.peerChanged = !paired_ || !macEqual(mac, peer_);
  macCopy(peer_, mac);
  peerRole_ = role;
  peerSide_ = remote;
  paired_ = true;
  return result;
}

void PairPort::fillAck(Frame& out) const {
  packPairIdentity(out, static_cast<uint8_t>(PairType::Ack), local_, localSide_, localRole_);
}

void PairPort::fillHello(Frame& out) const {
  packPairIdentity(out, static_cast<uint8_t>(PairType::Hello), local_, localSide_, localRole_);
}

}  // namespace bahn
