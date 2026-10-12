#pragma once

#include <stdint.h>

#include "protocol.h"

namespace bahn {

// Temporary pairing cable. Both ends use the same socket pinout:
//   pin 1 GND, pin 2 TX, pin 3 RX.
// The cable crosses pin 2 and pin 3 and ties the grounds together.
// A pin value of -1 means this module has no socket on that side.
constexpr int kPairNone = -1;
constexpr uint32_t kPairBaud = 115200;
constexpr uint32_t kPairHelloIntervalMs = 300;

// Onboard on both relay boards. The boot button is GPIO0 (active low).
// Holding it during reset starts the serial bootloader.
// The status LED is GPIO23, active high.
constexpr int kLedPin = 23;
constexpr int kButtonPin = 0;

// Block module: two block signals and the axle counter, no desk buttons.
// Station module (pio env bahnhof): three track buttons, the line button and
// the entry-signal button. The line faces kStreckeSeite.
#ifndef MODULE_BAHNHOF
#define MODULE_BAHNHOF 0
#endif
constexpr ModuleRole kModuleRole = MODULE_BAHNHOF ? ModuleRole::Bahnhof : ModuleRole::Block;
constexpr Side kStreckeSeite = Side::B;
constexpr uint8_t kRichtungAusfahrt = 1;
constexpr uint32_t kTasteDebounceMs = 25;
// Open from boot on the station. WPA2 needs 8 to 63 characters.
constexpr const char* kWebSsid = "Bahnhof";
constexpr const char* kWebPass = "bahnhof1";

// Relay outputs are active high and are fixed by the board.
// Block (ESP32_Relay_X2): GPIO16 signal side A, GPIO17 signal side B.
// Station (ESP32_Relay_X8): each signal has a Halt/Frei changeover relay and a
// yellow relay. GPIO32 and GPIO33 are the exit signal, GPIO25 and GPIO26 the
// entry signal. Low on the changeover relay is Halt, high is Frei. The yellow
// relay is high only for Frei 40. GPIO27, 14, 12, 13 are the turnout relays.
//
// The station pairs only on side B (the line). GPIO34–39 have no internal
// pull-up, so those inputs need 10 kΩ to 3.3 V.
#if MODULE_BAHNHOF
constexpr int kPairARxPin = kPairNone;
constexpr int kPairATxPin = kPairNone;
constexpr int kPairBRxPin = 34;
constexpr int kPairBTxPin = 17;
constexpr int kGleis1Pin = 4;
constexpr int kGleis2Pin = 39;
constexpr int kGleis3Pin = 18;
constexpr int kEinfahrtPin = 19;
// GPIO5 is a strapping pin. The pull-up keeps it high at boot; do not hold
// the line key down while the module resets.
constexpr int kStreckePin = 5;
// One relay per turnout. Low is Stellung 0, high is Stellung 1.
// GPIO12 must stay low through reset; Stellung 0 is that level.
constexpr int kWeiche1Pin = 27;
constexpr int kWeiche2Pin = 14;
constexpr int kWeiche3Pin = 12;
constexpr int kWeiche4Pin = 13;
// Changeover relay: low rests at Halt, high switches to Frei.
// The yellow relay adds the second lamp and is high only for Frei 40.
constexpr int kAusfahrtFreiPin = 32;
constexpr int kAusfahrtGelbPin = 33;
constexpr int kEinfahrtFreiPin = 25;
constexpr int kEinfahrtGelbPin = 26;
constexpr int kAxleAPin = 35;
constexpr int kAxleBPin = 36;
#else
constexpr int kPairARxPin = 14;
constexpr int kPairATxPin = 27;
constexpr int kPairBRxPin = 25;
constexpr int kPairBTxPin = 26;
constexpr int kAxleAPin = 32;
constexpr int kAxleBPin = 33;
#endif
constexpr uint32_t kAxleDebounceMs = 8;
constexpr uint32_t kAxlePairWindowMs = 2000;
// Rückblock is sent only after both counts have stopped changing. A pause
// longer than this between axles is treated as the end of the train.
constexpr uint32_t kAxleSettleMs = 1500;

// Two MAX7219 8x8 matrices on the station, daisy-chained.
// Display 1 (departure) takes DIN from the ESP32. Display 2 (entry) takes
// DIN from display 1 DOUT. Both share CLK and CS. VCC is 5 V.
// CLK is GPIO15 (idle low). DIN is GPIO2. CS is GPIO16 and idles low.
constexpr int kMatrixDinPin = 2;
constexpr int kMatrixClkPin = 15;
constexpr int kMatrixCsPin = 16;
constexpr uint8_t kMatrixIntensity = 4;

// Monochrome 128x64 SSD1306 on I2C. Most modules answer at 0x3C.
// VCC is 3.3 V. SDA is GPIO21, SCL is GPIO22.
constexpr int kOledSdaPin = 21;
constexpr int kOledSclPin = 22;
constexpr uint8_t kOledI2CAddress = 0x3C;

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
