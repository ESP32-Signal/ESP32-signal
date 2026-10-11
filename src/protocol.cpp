#include "protocol.h"

#include <stdio.h>
#include <string.h>

namespace bahn {

namespace {

uint16_t read16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

void write16(uint8_t* p, uint16_t value) {
  p[0] = static_cast<uint8_t>(value & 0xFF);
  p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

}  // namespace

FrameParser::FrameParser(uint8_t magic0, uint8_t magic1) : magic0_(magic0), magic1_(magic1) {}

void FrameParser::reset() { index_ = 0; }

bool FrameParser::feed(uint8_t byte, Frame& out) {
  if (index_ == 0) {
    if (byte != magic0_) {
      return false;
    }
    buf_[index_++] = byte;
    return false;
  }
  if (index_ == 1) {
    if (byte != magic1_) {
      index_ = (byte == magic0_) ? 1 : 0;
      if (index_ == 1) {
        buf_[0] = magic0_;
      }
      return false;
    }
    buf_[index_++] = byte;
    return false;
  }

  buf_[index_++] = byte;
  if (index_ < kFrameSize) {
    return false;
  }
  index_ = 0;
  return decodeFrame(magic0_, magic1_, buf_, kFrameSize, out);
}

uint16_t crc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int bit = 0; bit < 8; ++bit) {
      if ((crc & 0x8000) != 0) {
        crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
      } else {
        crc = static_cast<uint16_t>(crc << 1);
      }
    }
  }
  return crc;
}

size_t encodeFrame(uint8_t magic0, uint8_t magic1, const Frame& frame, uint8_t* out, size_t cap) {
  if (out == nullptr || cap < kFrameSize || frame.len > kPayloadSize) {
    return 0;
  }
  memset(out, 0, kFrameSize);
  out[0] = magic0;
  out[1] = magic1;
  out[2] = kProtocolVersion;
  out[3] = frame.type;
  out[4] = frame.seq;
  write16(out + 5, frame.bootId);
  out[7] = frame.len;
  memcpy(out + 8, frame.payload, frame.len);
  write16(out + 24, crc16(out, 24));
  return kFrameSize;
}

bool decodeFrame(uint8_t magic0, uint8_t magic1, const uint8_t* data, size_t len, Frame& out) {
  if (data == nullptr || len != kFrameSize) {
    return false;
  }
  if (data[0] != magic0 || data[1] != magic1 || data[2] != kProtocolVersion) {
    return false;
  }
  if (data[7] > kPayloadSize) {
    return false;
  }
  const uint16_t expect = crc16(data, 24);
  if (read16(data + 24) != expect) {
    return false;
  }
  out = Frame{};
  out.type = data[3];
  out.seq = data[4];
  out.bootId = read16(data + 5);
  out.len = data[7];
  memcpy(out.payload, data + 8, kPayloadSize);
  return true;
}

void packPairIdentity(Frame& frame, uint8_t type, const uint8_t mac[kMacSize], Side side, ModuleRole role) {
  frame = Frame{};
  frame.type = type;
  frame.len = 8;
  macCopy(frame.payload, mac);
  frame.payload[6] = static_cast<uint8_t>(side);
  frame.payload[7] = static_cast<uint8_t>(role);
}

bool unpackPairIdentity(const Frame& frame, uint8_t mac[kMacSize], Side& side, ModuleRole& role) {
  if (frame.len < 8 || frame.payload[6] > 1) {
    return false;
  }
  macCopy(mac, frame.payload);
  if (!macValid(mac)) {
    return false;
  }
  side = static_cast<Side>(frame.payload[6]);
  const uint8_t rawRole = frame.payload[7];
  if (rawRole == static_cast<uint8_t>(ModuleRole::Block)) {
    role = ModuleRole::Block;
  } else if (rawRole == static_cast<uint8_t>(ModuleRole::Bahnhof)) {
    role = ModuleRole::Bahnhof;
  } else {
    role = ModuleRole::Unknown;
  }
  return true;
}

void packAchszahl(Frame& frame, MsgType type, uint8_t seq, uint16_t bootId, uint16_t achszahl) {
  frame = Frame{};
  frame.type = static_cast<uint8_t>(type);
  frame.seq = seq;
  frame.bootId = bootId;
  frame.len = 2;
  write16(frame.payload, achszahl);
}

bool unpackAchszahl(const Frame& frame, uint16_t& achszahl) {
  const uint8_t type = frame.type;
  const bool axle = type == static_cast<uint8_t>(MsgType::Vorblock) ||
                    type == static_cast<uint8_t>(MsgType::Rueckblock) ||
                    type == static_cast<uint8_t>(MsgType::Achszahl);
  if (!axle || frame.len < 2) {
    return false;
  }
  achszahl = read16(frame.payload);
  return true;
}

void packFahrtrichtung(Frame& frame, uint8_t seq, uint16_t bootId, FahrtrichtungBefehl befehl, uint8_t richtung) {
  frame = Frame{};
  frame.type = static_cast<uint8_t>(MsgType::Fahrtrichtung);
  frame.seq = seq;
  frame.bootId = bootId;
  frame.len = 2;
  frame.payload[0] = static_cast<uint8_t>(befehl);
  frame.payload[1] = richtung;
}

bool unpackFahrtrichtung(const Frame& frame, FahrtrichtungBefehl& befehl, uint8_t& richtung) {
  if (frame.type != static_cast<uint8_t>(MsgType::Fahrtrichtung) || frame.len < 2) {
    return false;
  }
  befehl = static_cast<FahrtrichtungBefehl>(frame.payload[0]);
  richtung = frame.payload[1];
  return true;
}

void packAck(Frame& frame, uint16_t bootId, uint8_t ackedSeq, uint8_t ackedType, uint16_t ackedBoot) {
  frame = Frame{};
  frame.type = static_cast<uint8_t>(MsgType::Ack);
  frame.bootId = bootId;
  frame.len = 4;
  frame.payload[0] = ackedSeq;
  frame.payload[1] = ackedType;
  write16(frame.payload + 2, ackedBoot);
}

bool unpackAck(const Frame& frame, uint8_t& ackedSeq, uint8_t& ackedType, uint16_t& ackedBoot) {
  if (frame.type != static_cast<uint8_t>(MsgType::Ack) || frame.len < 4) {
    return false;
  }
  ackedSeq = frame.payload[0];
  ackedType = frame.payload[1];
  ackedBoot = read16(frame.payload + 2);
  return true;
}

bool macEqual(const uint8_t* a, const uint8_t* b) {
  return memcmp(a, b, kMacSize) == 0;
}

bool macValid(const uint8_t* mac) {
  bool any = false;
  for (size_t i = 0; i < kMacSize; ++i) {
    if (mac[i] != 0) {
      any = true;
      break;
    }
  }
  if (!any) {
    return false;
  }
  return (mac[0] & 0x01) == 0;
}

void macCopy(uint8_t* dst, const uint8_t* src) { memcpy(dst, src, kMacSize); }

void formatMac(const uint8_t* mac, char* out, size_t cap) {
  if (out == nullptr || cap == 0) {
    return;
  }
  if (mac == nullptr || cap < 18) {
    out[0] = '\0';
    return;
  }
  snprintf(out, cap, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool validSide(Side side) { return side == Side::A || side == Side::B; }

const char* sideName(Side side) { return side == Side::B ? "B" : "A"; }

const char* roleName(ModuleRole role) {
  switch (role) {
    case ModuleRole::Block:
      return "block";
    case ModuleRole::Bahnhof:
      return "bahnhof";
    case ModuleRole::Unknown:
      break;
  }
  return "unknown";
}

const char* msgTypeName(uint8_t type) {
  switch (static_cast<MsgType>(type)) {
    case MsgType::Heartbeat:
      return "heartbeat";
    case MsgType::Ack:
      return "ack";
    case MsgType::Vorblock:
      return "vorblock";
    case MsgType::Rueckblock:
      return "rueckblock";
    case MsgType::Achszahl:
      return "achszahl";
    case MsgType::Fahrtrichtung:
      return "fahrtrichtung";
    default:
      return "unknown";
  }
}

}  // namespace bahn
