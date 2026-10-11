#pragma once

#include <stdint.h>

namespace bahn {

// Temporary pairing cable. Both modules use the same socket pinout:
//   pin 1 GND, pin 2 TX, pin 3 RX.
// The cable crosses pin 2 and pin 3 and ties the grounds together.
// Plug it into the socket that faces that neighbor, then remove it.
// Unpaired sockets announce on their own; a paired socket stays quiet
// until a different module answers on that cable.
//
// Side A socket: TX GPIO27, RX GPIO14
// Side B socket: TX GPIO26, RX GPIO25
constexpr int kPairARxPin = 14;
constexpr int kPairATxPin = 27;
constexpr int kPairBRxPin = 25;
constexpr int kPairBTxPin = 26;
constexpr uint32_t kPairBaud = 115200;
constexpr uint32_t kPairHelloIntervalMs = 300;

// ESP32_Relay_X2 onboard parts. The boot button is GPIO0 (active low).
// Holding it during reset starts the serial bootloader.
// Relay outputs are GPIO16 and GPIO17; this firmware does not drive them.
constexpr int kLedPin = 23;
constexpr int kButtonPin = 0;

// Axle sensor at this block boundary. Contact A sits on the side-A side of
// the pair, contact B on the side-B side. Both switches close to GND.
constexpr int kAxleAPin = 32;
constexpr int kAxleBPin = 33;
constexpr uint32_t kAxleDebounceMs = 8;
constexpr uint32_t kAxlePairWindowMs = 2000;
// Rückblock is sent only after both counts have stopped changing. A pause
// longer than this between axles is treated as the end of the train.
constexpr uint32_t kAxleSettleMs = 1500;

// All modules must share this channel. Peer entries use channel 0, which
// follows the channel locked here.
constexpr uint8_t kWifiChannel = 1;

constexpr uint32_t kHeartbeatIntervalMs = 1000;
constexpr uint32_t kOnlineTimeoutMs = 3500;
constexpr uint32_t kRadioAckTimeoutMs = 150;
constexpr uint32_t kRadioCallbackTimeoutMs = 500;
constexpr uint8_t kMaxSendAttempts = 5;
constexpr uint8_t kTxQueueDepth = 4;
constexpr uint8_t kAckQueueDepth = 4;

}  // namespace bahn
