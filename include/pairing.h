#pragma once

#include <Arduino.h>

#include "pair_port.h"

namespace bahn {

// Two UART sockets, one per neighbor. Runs the cable handshake and reports
// each new peer to the radio layer.
class Pairing {
 public:
  using Handler = void (*)(Side localSide, const uint8_t mac[kMacSize], ModuleRole role, Side remoteSide);

  void begin(const uint8_t localMac[kMacSize], ModuleRole role);
  void onDiscovered(Handler handler);
  void restore(Side side, const uint8_t mac[kMacSize], ModuleRole role, Side remoteSide);
  void forget(Side side);
  void loop();

 private:
  void service(HardwareSerial& serial, FrameParser& parser, PairPort& port);

  PairPort port_[2];
  FrameParser parser_[2] = {
      FrameParser(kCableMagic0, kCableMagic1),
      FrameParser(kCableMagic0, kCableMagic1),
  };
  Handler handler_ = nullptr;
  bool begun_ = false;
};

}  // namespace bahn
