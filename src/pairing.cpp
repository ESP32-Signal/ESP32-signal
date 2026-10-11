#include "pairing.h"

#include "config.h"

namespace bahn {
namespace {

void startUart(HardwareSerial& serial, int rxPin, int txPin) {
  serial.begin(kPairBaud, SERIAL_8N1, rxPin, txPin);
  gpio_pullup_en(static_cast<gpio_num_t>(rxPin));
}

void writeFrame(HardwareSerial& serial, const Frame& frame) {
  uint8_t buf[kFrameSize];
  const size_t n = encodeFrame(kCableMagic0, kCableMagic1, frame, buf, sizeof(buf));
  if (n > 0) {
    serial.write(buf, n);
  }
}

}  // namespace

void Pairing::begin(const uint8_t localMac[kMacSize], ModuleRole role) {
  port_[0].init(localMac, Side::A, role);
  port_[1].init(localMac, Side::B, role);
  parser_[0].reset();
  parser_[1].reset();
  startUart(Serial1, kPairARxPin, kPairATxPin);
  startUart(Serial2, kPairBRxPin, kPairBTxPin);
  begun_ = true;
}

void Pairing::onDiscovered(Handler handler) { handler_ = handler; }

void Pairing::restore(Side side, const uint8_t mac[kMacSize], ModuleRole role, Side remoteSide) {
  if (!validSide(side)) {
    return;
  }
  port_[static_cast<size_t>(side)].restore(mac, role, remoteSide);
}

void Pairing::forget(Side side) {
  if (!validSide(side)) {
    return;
  }
  const size_t index = static_cast<size_t>(side);
  port_[index].forget();
  parser_[index].reset();
}

void Pairing::loop() {
  if (!begun_) {
    return;
  }
  service(Serial1, parser_[0], port_[0]);
  service(Serial2, parser_[1], port_[1]);
}

void Pairing::service(HardwareSerial& serial, FrameParser& parser, PairPort& port) {
  while (serial.available() > 0) {
    const int raw = serial.read();
    if (raw < 0) {
      break;
    }
    Frame frame;
    if (!parser.feed(static_cast<uint8_t>(raw), frame)) {
      continue;
    }
    const PairRxResult result = port.onFrame(frame);
    if (result.sendAck) {
      Frame ack;
      port.fillAck(ack);
      writeFrame(serial, ack);
    }
    if (result.peerChanged && handler_ != nullptr) {
      handler_(port.localSide(), port.peerMac(), port.peerRole(), port.peerSide());
    }
  }

  Frame hello;
  if (port.helloDue(millis(), hello)) {
    writeFrame(serial, hello);
  }
}

}  // namespace bahn
