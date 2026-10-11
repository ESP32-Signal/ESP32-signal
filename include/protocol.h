#pragma once

#include <stddef.h>
#include <stdint.h>

namespace bahn {

// Neighbor link between block modules.
//
// Cable frames and radio frames share one layout so a pairing exchange and
// an ESP-NOW message can be tested with the same codec. Magic bytes keep the
// two buses apart.
//
//   0      magic0
//   1      magic1
//   2      version
//   3      type
//   4      seq
//   5..6   boot id, little endian
//   7      payload length
//   8..23  payload (unused bytes are zero and still covered by the CRC)
//   24..25 CRC-16/CCITT-FALSE over bytes 0..23, little endian

constexpr uint8_t kCableMagic0 = 0xC0;
constexpr uint8_t kCableMagic1 = 0xA1;
constexpr uint8_t kRadioMagic0 = 0xE5;
constexpr uint8_t kRadioMagic1 = 0x51;
constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kMacSize = 6;
constexpr size_t kPayloadSize = 16;
constexpr size_t kFrameSize = 26;

enum class Side : uint8_t { A = 0, B = 1 };

enum class ModuleRole : uint8_t {
  Unknown = 0,
  Block = 1,
  Bahnhof = 2,
};

enum class MsgType : uint8_t {
  Heartbeat = 1,
  Ack = 2,
  Vorblock = 0x10,
  Rueckblock = 0x11,
  Achszahl = 0x12,
  Fahrtrichtung = 0x13,
};

enum class PairType : uint8_t {
  Hello = 1,
  Ack = 2,
};

// Command carried inside a Fahrtrichtung message. The direction byte is
// defined by the block logic; this layer only transports it.
enum class FahrtrichtungBefehl : uint8_t {
  Anfragen = 1,
  Festlegen = 2,
  Freigeben = 3,
  Status = 4,
};

struct Frame {
  uint8_t type = 0;
  uint8_t seq = 0;
  uint16_t bootId = 0;
  uint8_t len = 0;
  uint8_t payload[kPayloadSize] = {};
};

class FrameParser {
 public:
  FrameParser(uint8_t magic0, uint8_t magic1);

  void reset();
  bool feed(uint8_t byte, Frame& out);

 private:
  uint8_t magic0_;
  uint8_t magic1_;
  uint8_t index_ = 0;
  uint8_t buf_[kFrameSize] = {};
};

uint16_t crc16(const uint8_t* data, size_t len);

size_t encodeFrame(uint8_t magic0, uint8_t magic1, const Frame& frame, uint8_t* out, size_t cap);
bool decodeFrame(uint8_t magic0, uint8_t magic1, const uint8_t* data, size_t len, Frame& out);

void packPairIdentity(Frame& frame, uint8_t type, const uint8_t mac[kMacSize], Side side, ModuleRole role);
bool unpackPairIdentity(const Frame& frame, uint8_t mac[kMacSize], Side& side, ModuleRole& role);

void packAchszahl(Frame& frame, MsgType type, uint8_t seq, uint16_t bootId, uint16_t achszahl);
bool unpackAchszahl(const Frame& frame, uint16_t& achszahl);

void packFahrtrichtung(Frame& frame, uint8_t seq, uint16_t bootId, FahrtrichtungBefehl befehl, uint8_t richtung);
bool unpackFahrtrichtung(const Frame& frame, FahrtrichtungBefehl& befehl, uint8_t& richtung);

void packAck(Frame& frame, uint16_t bootId, uint8_t ackedSeq, uint8_t ackedType, uint16_t ackedBoot);
bool unpackAck(const Frame& frame, uint8_t& ackedSeq, uint8_t& ackedType, uint16_t& ackedBoot);

bool macEqual(const uint8_t* a, const uint8_t* b);
bool macValid(const uint8_t* mac);
void macCopy(uint8_t* dst, const uint8_t* src);
void formatMac(const uint8_t* mac, char* out, size_t cap);

bool validSide(Side side);
const char* sideName(Side side);
const char* roleName(ModuleRole role);
const char* msgTypeName(uint8_t type);

}  // namespace bahn
