#include "pair_port.h"
#include "protocol.h"

#include <stdio.h>
#include <string.h>

namespace {

int failures = 0;

void expect(bool cond, const char* name) {
  if (cond) {
    return;
  }
  fprintf(stderr, "FAIL %s\n", name);
  failures++;
}

bool roundTrip(const bahn::Frame& frame, uint8_t magic0, uint8_t magic1, bahn::Frame& out) {
  uint8_t buf[bahn::kFrameSize];
  if (bahn::encodeFrame(magic0, magic1, frame, buf, sizeof(buf)) != bahn::kFrameSize) {
    return false;
  }
  return bahn::decodeFrame(magic0, magic1, buf, bahn::kFrameSize, out);
}

bool feedAll(bahn::FrameParser& parser, const uint8_t* data, size_t len, bahn::Frame& out) {
  bool got = false;
  for (size_t i = 0; i < len; ++i) {
    bahn::Frame frame;
    if (!parser.feed(data[i], frame)) {
      continue;
    }
    out = frame;
    got = true;
  }
  return got;
}

void testCodec() {
  bahn::Frame in;
  bahn::packAchszahl(in, bahn::MsgType::Vorblock, 7, 0x1234, 42);
  bahn::Frame out;
  expect(roundTrip(in, bahn::kRadioMagic0, bahn::kRadioMagic1, out), "roundtrip");
  expect(out.type == static_cast<uint8_t>(bahn::MsgType::Vorblock), "type");
  expect(out.seq == 7, "seq");
  expect(out.bootId == 0x1234, "boot");
  uint16_t axles = 0;
  expect(bahn::unpackAchszahl(out, axles) && axles == 42, "axles");

  uint8_t buf[bahn::kFrameSize];
  bahn::encodeFrame(bahn::kRadioMagic0, bahn::kRadioMagic1, in, buf, sizeof(buf));
  buf[10] ^= 0xFF;
  expect(!bahn::decodeFrame(bahn::kRadioMagic0, bahn::kRadioMagic1, buf, sizeof(buf), out), "bad crc");
  expect(!bahn::decodeFrame(bahn::kCableMagic0, bahn::kCableMagic1, buf, sizeof(buf), out), "wrong magic");

  bahn::packFahrtrichtung(in, 3, 9, bahn::FahrtrichtungBefehl::Festlegen, 1);
  expect(roundTrip(in, bahn::kRadioMagic0, bahn::kRadioMagic1, out), "richtung roundtrip");
  bahn::FahrtrichtungBefehl befehl = bahn::FahrtrichtungBefehl::Anfragen;
  uint8_t richtung = 0;
  expect(bahn::unpackFahrtrichtung(out, befehl, richtung), "unpack richtung");
  expect(befehl == bahn::FahrtrichtungBefehl::Festlegen && richtung == 1, "richtung fields");

  bahn::packAck(in, 5, 7, static_cast<uint8_t>(bahn::MsgType::Achszahl), 0x1234);
  expect(roundTrip(in, bahn::kRadioMagic0, bahn::kRadioMagic1, out), "ack roundtrip");
  uint8_t seq = 0;
  uint8_t type = 0;
  uint16_t boot = 0;
  expect(bahn::unpackAck(out, seq, type, boot), "unpack ack");
  expect(seq == 7 && type == static_cast<uint8_t>(bahn::MsgType::Achszahl) && boot == 0x1234, "ack fields");
}

void testParser() {
  bahn::Frame in;
  bahn::packAchszahl(in, bahn::MsgType::Rueckblock, 2, 8, 11);
  uint8_t good[bahn::kFrameSize];
  bahn::encodeFrame(bahn::kCableMagic0, bahn::kCableMagic1, in, good, sizeof(good));

  uint8_t stream[8 + bahn::kFrameSize * 2];
  memset(stream, 0xA5, 8);
  memcpy(stream + 8, good, bahn::kFrameSize);
  memcpy(stream + 8 + bahn::kFrameSize, good, bahn::kFrameSize);

  bahn::FrameParser parser(bahn::kCableMagic0, bahn::kCableMagic1);
  bahn::Frame first;
  bahn::Frame second;
  size_t found = 0;
  for (size_t i = 0; i < sizeof(stream); ++i) {
    bahn::Frame frame;
    if (!parser.feed(stream[i], frame)) {
      continue;
    }
    if (found == 0) {
      first = frame;
    } else {
      second = frame;
    }
    found++;
  }
  expect(found == 2, "parser found two");
  uint16_t axles = 0;
  expect(bahn::unpackAchszahl(first, axles) && axles == 11, "parser payload");
  expect(second.seq == 2, "parser second");

  uint8_t bad[bahn::kFrameSize];
  memcpy(bad, good, sizeof(bad));
  bad[bahn::kFrameSize - 1] ^= 0x55;
  uint8_t recover[bahn::kFrameSize * 2];
  memcpy(recover, bad, bahn::kFrameSize);
  memcpy(recover + bahn::kFrameSize, good, bahn::kFrameSize);
  parser.reset();
  bahn::Frame recovered;
  expect(feedAll(parser, recover, sizeof(recover), recovered), "resync after bad crc");
  expect(recovered.seq == 2, "resynced seq");
}

void testMac() {
  const uint8_t ok[6] = {0x24, 0x6F, 0x28, 0x11, 0x22, 0x33};
  const uint8_t zero[6] = {};
  const uint8_t multi[6] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x02};
  expect(bahn::macValid(ok), "unicast valid");
  expect(!bahn::macValid(zero), "zero rejected");
  expect(!bahn::macValid(multi), "multicast rejected");
  char text[18];
  bahn::formatMac(ok, text, sizeof(text));
  expect(strcmp(text, "24:6F:28:11:22:33") == 0, "format mac");
}

void exchange(bahn::PairPort& dst, const bahn::Frame& tx, bahn::PairRxResult& result, bahn::Frame& ack) {
  uint8_t buf[bahn::kFrameSize];
  expect(bahn::encodeFrame(bahn::kCableMagic0, bahn::kCableMagic1, tx, buf, sizeof(buf)) == bahn::kFrameSize,
         "encode pair");
  bahn::FrameParser parser(bahn::kCableMagic0, bahn::kCableMagic1);
  bahn::Frame rx;
  expect(feedAll(parser, buf, sizeof(buf), rx), "parse pair");
  result = dst.onFrame(rx);
  if (result.sendAck) {
    dst.fillAck(ack);
  }
}

void testPairing() {
  const uint8_t macA[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x0A};
  const uint8_t macB[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x0B};
  const uint8_t macC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x0C};

  bahn::PairPort portA;
  bahn::PairPort portB;
  portA.init(macA, bahn::Side::A, bahn::ModuleRole::Block);
  portB.init(macB, bahn::Side::B, bahn::ModuleRole::Bahnhof);

  bahn::Frame hello;
  expect(portA.helloDue(0, hello), "first hello");
  expect(!portA.helloDue(100, hello), "hello waits");
  expect(portA.helloDue(1000, hello), "hello repeats");

  bahn::PairRxResult result;
  bahn::Frame ack;
  exchange(portB, hello, result, ack);
  expect(result.peerChanged && result.sendAck, "B learns A");
  expect(portB.paired() && bahn::macEqual(portB.peerMac(), macA), "B stored A");
  expect(portB.peerSide() == bahn::Side::A, "B saw remote side A");
  expect(portB.peerRole() == bahn::ModuleRole::Block, "B saw block");
  expect(!portB.helloDue(5000, hello), "paired port stays quiet");

  exchange(portA, ack, result, ack);
  expect(result.peerChanged && !result.sendAck, "A learns B from ack");
  expect(portA.paired() && bahn::macEqual(portA.peerMac(), macB), "A stored B");
  expect(portA.peerRole() == bahn::ModuleRole::Bahnhof, "A saw bahnhof");
  expect(portA.peerSide() == bahn::Side::B, "A saw remote side B");

  // B is paired, so it does not hello. A Hello from the same peer must not count as a new pair,
  // but it must still be acknowledged so a peer that missed the first Ack can finish.
  bahn::Frame helloB;
  bahn::packPairIdentity(helloB, static_cast<uint8_t>(bahn::PairType::Hello), macB, bahn::Side::B,
                           bahn::ModuleRole::Bahnhof);
  exchange(portA, helloB, result, ack);
  expect(!result.peerChanged && result.sendAck, "repeat hello is not a new pair");

  bahn::Frame helloC;
  bahn::packPairIdentity(helloC, static_cast<uint8_t>(bahn::PairType::Hello), macC, bahn::Side::A,
                           bahn::ModuleRole::Block);
  exchange(portA, helloC, result, ack);
  expect(result.peerChanged && bahn::macEqual(portA.peerMac(), macC), "new cable replaces peer");

  portA.forget();
  expect(!portA.paired(), "forget");
  expect(portA.helloDue(0, hello), "forgotten port announces");

  bahn::Frame selfHello;
  bahn::packPairIdentity(selfHello, static_cast<uint8_t>(bahn::PairType::Hello), macA, bahn::Side::B,
                           bahn::ModuleRole::Block);
  exchange(portA, selfHello, result, ack);
  expect(!result.peerChanged && !portA.paired(), "reject own mac");

  bahn::Frame restoredHello;
  portA.restore(macB, bahn::ModuleRole::Block, bahn::Side::B);
  expect(portA.paired() && !portA.helloDue(0, restoredHello), "restore stays quiet");
}

}  // namespace

int main() {
  testCodec();
  testParser();
  testMac();
  testPairing();
  if (failures != 0) {
    fprintf(stderr, "%d failed\n", failures);
    return 1;
  }
  printf("ok\n");
  return 0;
}
