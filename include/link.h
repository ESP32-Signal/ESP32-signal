#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <Preferences.h>
#include <stdint.h>

#include "config.h"
#include "pairing.h"
#include "protocol.h"

namespace bahn {

// One received block message. achszahl is set for Vorblock, Rückblock and
// Achszahl. befehl and richtung are set for Fahrtrichtung.
struct Received {
  Side from = Side::A;
  MsgType type = MsgType::Heartbeat;
  uint8_t seq = 0;
  uint16_t achszahl = 0;
  FahrtrichtungBefehl befehl = FahrtrichtungBefehl::Status;
  uint8_t richtung = 0;
};

// ESP-NOW link to the two paired neighbors. Call begin() once, then loop()
// from the main loop. Sends are queued; onDelivery reports the radio result
// after the neighbor's application ack, or failure after the retries.
class Link {
 public:
  static Link& instance();

  void begin(ModuleRole role = ModuleRole::Block);
  void loop();

  bool paired(Side side) const;
  bool online(Side side) const;
  ModuleRole peerRole(Side side) const;
  Side remoteSide(Side side) const;
  bool peerMac(Side side, uint8_t mac[kMacSize]) const;
  void clearPeer(Side side);

  bool sendVorblock(Side side, uint16_t achszahl, uint8_t* seq = nullptr);
  bool sendRueckblock(Side side, uint16_t achszahl, uint8_t* seq = nullptr);
  bool sendAchszahl(Side side, uint16_t achszahl, uint8_t* seq = nullptr);
  bool sendFahrtrichtung(Side side, FahrtrichtungBefehl befehl, uint8_t richtung, uint8_t* seq = nullptr);

  using ReceiveHandler = void (*)(const Received& message);
  using PairHandler = void (*)(Side localSide, const uint8_t mac[kMacSize], ModuleRole role, Side remoteSide);
  using DeliveryHandler = void (*)(Side to, MsgType type, uint8_t seq, bool ok);

  void onReceive(ReceiveHandler handler);
  void onPaired(PairHandler handler);
  void onDelivery(DeliveryHandler handler);

  const uint8_t* localMac() const { return localMac_; }
  void logStatus() const;

  Link(const Link&) = delete;
  Link& operator=(const Link&) = delete;

 private:
  Link() = default;

  struct TxQueue {
    Frame items[kTxQueueDepth];
    uint8_t head = 0;
    uint8_t count = 0;
  };

  struct SideState {
    bool paired = false;
    bool online = false;
    bool haveRx = false;
    ModuleRole role = ModuleRole::Unknown;
    Side remote = Side::A;
    uint8_t mac[kMacSize] = {};
    uint32_t lastRxMs = 0;
    uint16_t lastBoot = 0;
    uint8_t lastSeq = 0;
    uint8_t nextSeq = 1;
    TxQueue queue;
  };

  enum class Phase : uint8_t { Idle, WaitingMac, WaitingApp, Retry };
  enum class RadioJob : uint8_t { None, Data, Ack, Heartbeat };

  struct Inflight {
    bool active = false;
    Phase phase = Phase::Idle;
    Side side = Side::A;
    uint8_t attempts = 0;
    uint32_t waitMark = 0;
    Frame frame;
  };

  struct PendingAck {
    uint8_t mac[kMacSize] = {};
    uint8_t attempts = 0;
    Frame frame;
  };

  struct RxEvent {
    uint8_t mac[kMacSize] = {};
    uint8_t len = 0;
    uint8_t data[32] = {};
  };

  struct TxEvent {
    uint8_t ok = 0;
  };

  void startWifi();
  bool startRadio();
  void ensureRadio();
  void loadPeers();
  void savePeer(Side side);
  void installPeer(Side side, const uint8_t mac[kMacSize], ModuleRole role, Side remoteSide, bool fromCable);
  void adoptPeer(Side side, const uint8_t mac[kMacSize], ModuleRole role, Side remoteSide);
  bool addRadioPeer(const uint8_t mac[kMacSize]);
  void removeRadioPeer(const uint8_t mac[kMacSize]);
  void resetRuntime(SideState& state);
  void cancelInflight(Side side);

  bool enqueue(Side side, const Frame& frame, uint8_t* seq);
  bool sideFromMac(const uint8_t mac[kMacSize], Side& side) const;
  SideState* state(Side side);
  const SideState* state(Side side) const;

  void handleRx(const uint8_t* mac, const uint8_t* data, int len);
  void onRadioRx(const RxEvent& event);
  void noteSendFinished(bool ok);
  void drainResults();
  void timeoutBusy();
  void applyResult(bool ok);
  void pumpRx();
  void pumpRadio();
  bool sendNextAck();
  bool serviceInflight();
  bool sendNextData();
  void sendHeartbeatIfDue();
  bool startSend(const uint8_t mac[kMacSize], const Frame& frame, RadioJob job);
  void queueAck(const uint8_t mac[kMacSize], uint8_t seq, uint8_t type, uint16_t boot);
  void popAck();
  void completeDelivery(bool ok);
  void touch(Side side);
  void refreshOnline();
  uint8_t allocSeq(SideState& sideState);

  static void discoverThunk(Side side, const uint8_t mac[kMacSize], ModuleRole role, Side remoteSide);

  SideState sides_[2];
  Inflight inflight_;
  PendingAck acks_[kAckQueueDepth];
  uint8_t ackHead_ = 0;
  uint8_t ackCount_ = 0;
  uint8_t rr_ = 0;
  uint8_t localMac_[kMacSize] = {};
  uint16_t bootId_ = 1;
  ModuleRole role_ = ModuleRole::Block;
  bool begun_ = false;
  bool radioOk_ = false;
  bool radioBusy_ = false;
  bool dropOne_ = false;
  RadioJob job_ = RadioJob::None;
  uint32_t radioStartMs_ = 0;
  uint32_t holdoffUntil_ = 0;
  uint32_t dropSinceMs_ = 0;
  uint32_t lastRadioRetryMs_ = 0;
  uint32_t lastHeartbeatMs_[2] = {};
  uint8_t hbCursor_ = 0;

  Pairing pairing_;
  Preferences prefs_;
  QueueHandle_t rxQueue_ = nullptr;
  QueueHandle_t txQueue_ = nullptr;

  ReceiveHandler onReceive_ = nullptr;
  PairHandler onPaired_ = nullptr;
  DeliveryHandler onDelivery_ = nullptr;

  friend void linkRxTrampoline(const uint8_t* mac, const uint8_t* data, int len);
  friend void linkTxTrampoline(bool ok);
};

}  // namespace bahn
