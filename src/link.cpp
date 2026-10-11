#include "link.h"

#include <esp_idf_version.h>
#include <esp_now.h>
#include <esp_system.h>
#include <esp_wifi.h>

#include <WiFi.h>

#include <string.h>

namespace bahn {

void linkRxTrampoline(const uint8_t* mac, const uint8_t* data, int len);
void linkTxTrampoline(bool ok);

namespace {

Link* activeLink = nullptr;

void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  (void)info;
  if (event == ARDUINO_EVENT_WIFI_STA_START) {
    esp_wifi_set_channel(kWifiChannel, WIFI_SECOND_CHAN_NONE);
  }
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void onEspNowRx(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (info == nullptr || info->src_addr == nullptr) {
    return;
  }
  linkRxTrampoline(info->src_addr, data, len);
}
#else
void onEspNowRx(const uint8_t* mac, const uint8_t* data, int len) { linkRxTrampoline(mac, data, len); }
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
void onEspNowTx(const esp_now_send_info_t* info, esp_now_send_status_t status) {
  (void)info;
  linkTxTrampoline(status == ESP_NOW_SEND_SUCCESS);
}
#else
void onEspNowTx(const uint8_t* mac, esp_now_send_status_t status) {
  (void)mac;
  linkTxTrampoline(status == ESP_NOW_SEND_SUCCESS);
}
#endif

}  // namespace

void linkRxTrampoline(const uint8_t* mac, const uint8_t* data, int len) {
  if (activeLink != nullptr) {
    activeLink->handleRx(mac, data, len);
  }
}

void linkTxTrampoline(bool ok) {
  if (activeLink != nullptr) {
    activeLink->noteSendFinished(ok);
  }
}

Link& Link::instance() {
  static Link link;
  return link;
}

void Link::begin(ModuleRole role) {
  role_ = role;
  activeLink = this;
  startWifi();
  WiFi.macAddress(localMac_);
  uint32_t mix = static_cast<uint32_t>(micros()) ^ esp_random();
  mix ^= static_cast<uint32_t>(localMac_[4]) << 16;
  mix ^= static_cast<uint32_t>(localMac_[5]) << 8;
  bootId_ = static_cast<uint16_t>(mix ^ (mix >> 16));
  if (bootId_ == 0) {
    bootId_ = 1;
  }

  if (rxQueue_ == nullptr) {
    rxQueue_ = xQueueCreate(8, sizeof(RxEvent));
  }
  if (txQueue_ == nullptr) {
    txQueue_ = xQueueCreate(4, sizeof(TxEvent));
  }

  if (!prefs_.begin("blocklink", false)) {
    Serial.println("NVS open failed");
  }
  loadPeers();

  pairing_.onDiscovered(&Link::discoverThunk);
  pairing_.begin(localMac_, role_);
  for (size_t i = 0; i < 2; ++i) {
    if (!sides_[i].paired) {
      continue;
    }
    pairing_.restore(static_cast<Side>(i), sides_[i].mac, sides_[i].role, sides_[i].remote);
  }

  radioOk_ = startRadio();
  if (radioOk_) {
    for (size_t i = 0; i < 2; ++i) {
      if (sides_[i].paired) {
        addRadioPeer(sides_[i].mac);
      }
    }
  }

  begun_ = true;
  char self[18];
  formatMac(localMac_, self, sizeof(self));
  Serial.printf("Block link ready  MAC %s  channel %u\n", self, kWifiChannel);
  logStatus();
}

void Link::loop() {
  if (!begun_) {
    return;
  }
  ensureRadio();
  pumpRx();
  pumpRadio();
  pairing_.loop();
  refreshOnline();
}

bool Link::paired(Side side) const {
  const SideState* node = state(side);
  return node != nullptr && node->paired;
}

bool Link::online(Side side) const {
  const SideState* node = state(side);
  return node != nullptr && node->online;
}

ModuleRole Link::peerRole(Side side) const {
  const SideState* node = state(side);
  return node != nullptr ? node->role : ModuleRole::Unknown;
}

Side Link::remoteSide(Side side) const {
  const SideState* node = state(side);
  return node != nullptr ? node->remote : Side::A;
}

bool Link::peerMac(Side side, uint8_t mac[kMacSize]) const {
  const SideState* node = state(side);
  if (node == nullptr || !node->paired || mac == nullptr) {
    return false;
  }
  macCopy(mac, node->mac);
  return true;
}

void Link::clearPeer(Side side) {
  if (!validSide(side)) {
    return;
  }
  SideState& node = sides_[static_cast<size_t>(side)];
  Frame dropped[kTxQueueDepth];
  uint8_t droppedCount = 0;
  bool dropInflight = false;
  uint8_t inflightType = 0;
  uint8_t inflightSeq = 0;
  if (inflight_.active && inflight_.side == side) {
    dropInflight = true;
    inflightType = inflight_.frame.type;
    inflightSeq = inflight_.frame.seq;
    if (radioBusy_ && job_ == RadioJob::Data) {
      dropOne_ = true;
      dropSinceMs_ = millis();
    }
    inflight_.active = false;
    inflight_.phase = Phase::Idle;
  }
  while (node.queue.count > 0 && droppedCount < kTxQueueDepth) {
    dropped[droppedCount++] = node.queue.items[node.queue.head];
    node.queue.head = static_cast<uint8_t>((node.queue.head + 1) % kTxQueueDepth);
    node.queue.count--;
  }
  if (node.paired) {
    removeRadioPeer(node.mac);
  }
  node = SideState{};
  savePeer(side);
  pairing_.forget(side);

  if (dropInflight) {
    Serial.printf("Send failed side %s %s seq %u\n", sideName(side), msgTypeName(inflightType), inflightSeq);
    if (onDelivery_ != nullptr) {
      onDelivery_(side, static_cast<MsgType>(inflightType), inflightSeq, false);
    }
  }
  for (uint8_t i = 0; i < droppedCount; ++i) {
    if (onDelivery_ != nullptr) {
      onDelivery_(side, static_cast<MsgType>(dropped[i].type), dropped[i].seq, false);
    }
  }
}

bool Link::sendVorblock(Side side, uint16_t achszahl, uint8_t* seq) {
  SideState* node = state(side);
  if (node == nullptr || !node->paired || node->queue.count >= kTxQueueDepth) {
    return false;
  }
  Frame frame;
  const uint8_t n = allocSeq(*node);
  packAchszahl(frame, MsgType::Vorblock, n, bootId_, achszahl);
  return enqueue(side, frame, seq);
}

bool Link::sendRueckblock(Side side, uint16_t achszahl, uint8_t* seq) {
  SideState* node = state(side);
  if (node == nullptr || !node->paired || node->queue.count >= kTxQueueDepth) {
    return false;
  }
  Frame frame;
  const uint8_t n = allocSeq(*node);
  packAchszahl(frame, MsgType::Rueckblock, n, bootId_, achszahl);
  return enqueue(side, frame, seq);
}

bool Link::sendAchszahl(Side side, uint16_t achszahl, uint8_t* seq) {
  SideState* node = state(side);
  if (node == nullptr || !node->paired || node->queue.count >= kTxQueueDepth) {
    return false;
  }
  Frame frame;
  const uint8_t n = allocSeq(*node);
  packAchszahl(frame, MsgType::Achszahl, n, bootId_, achszahl);
  return enqueue(side, frame, seq);
}

bool Link::sendFahrtrichtung(Side side, FahrtrichtungBefehl befehl, uint8_t richtung, uint8_t* seq) {
  SideState* node = state(side);
  if (node == nullptr || !node->paired || node->queue.count >= kTxQueueDepth) {
    return false;
  }
  Frame frame;
  const uint8_t n = allocSeq(*node);
  packFahrtrichtung(frame, n, bootId_, befehl, richtung);
  return enqueue(side, frame, seq);
}

void Link::onReceive(ReceiveHandler handler) { onReceive_ = handler; }

void Link::onPaired(PairHandler handler) { onPaired_ = handler; }

void Link::onDelivery(DeliveryHandler handler) { onDelivery_ = handler; }

void Link::logStatus() const {
  for (size_t i = 0; i < 2; ++i) {
    const Side side = static_cast<Side>(i);
    const SideState& node = sides_[i];
    if (!node.paired) {
      Serial.printf("Side %s: not paired\n", sideName(side));
      continue;
    }
    char peer[18];
    formatMac(node.mac, peer, sizeof(peer));
    Serial.printf("Side %s: %s  remote %s  %s  %s\n", sideName(side), peer, sideName(node.remote),
                  roleName(node.role), node.online ? "online" : "offline");
  }
}

void Link::startWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.onEvent(onWifiEvent);
  WiFi.disconnect(false, false);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  // set_channel is ignored until the interface is up; promiscuous mode is the
  // reliable way to lock it while the station is not associated.
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(kWifiChannel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  delay(100);
}

bool Link::startRadio() {
  const esp_err_t init = esp_now_init();
  if (init != ESP_OK && init != ESP_ERR_ESPNOW_EXIST) {
    Serial.println("ESP-NOW init failed");
    return false;
  }
  esp_wifi_set_channel(kWifiChannel, WIFI_SECOND_CHAN_NONE);
  if (esp_now_register_recv_cb(onEspNowRx) != ESP_OK || esp_now_register_send_cb(onEspNowTx) != ESP_OK) {
    Serial.println("ESP-NOW callbacks failed");
    return false;
  }
  return true;
}

void Link::ensureRadio() {
  if (radioOk_) {
    return;
  }
  const uint32_t now = millis();
  if (lastRadioRetryMs_ != 0 && (now - lastRadioRetryMs_) < 2000) {
    return;
  }
  lastRadioRetryMs_ = now;
  if (!startRadio()) {
    return;
  }
  radioOk_ = true;
  for (size_t i = 0; i < 2; ++i) {
    if (sides_[i].paired) {
      addRadioPeer(sides_[i].mac);
    }
  }
  Serial.println("ESP-NOW ready");
}

void Link::loadPeers() {
  for (size_t i = 0; i < 2; ++i) {
    const bool a = i == 0;
    if (prefs_.getUChar(a ? "okA" : "okB", 0) != 1) {
      continue;
    }
    uint8_t mac[kMacSize] = {};
    if (prefs_.getBytes(a ? "macA" : "macB", mac, kMacSize) != kMacSize || !macValid(mac)) {
      continue;
    }
    const auto role = static_cast<ModuleRole>(prefs_.getUChar(a ? "roleA" : "roleB", 0));
    const Side remote = prefs_.getUChar(a ? "psA" : "psB", 0) == 1 ? Side::B : Side::A;
    installPeer(static_cast<Side>(i), mac, role, remote, false);
  }
}

void Link::savePeer(Side side) {
  if (!validSide(side)) {
    return;
  }
  const bool a = side == Side::A;
  const SideState& node = sides_[static_cast<size_t>(side)];
  prefs_.putBytes(a ? "macA" : "macB", node.mac, kMacSize);
  prefs_.putUChar(a ? "roleA" : "roleB", static_cast<uint8_t>(node.role));
  prefs_.putUChar(a ? "psA" : "psB", static_cast<uint8_t>(node.remote));
  prefs_.putUChar(a ? "okA" : "okB", node.paired ? 1 : 0);
}

void Link::installPeer(Side side, const uint8_t mac[kMacSize], ModuleRole role, Side remoteSide, bool fromCable) {
  if (!validSide(side) || !macValid(mac)) {
    return;
  }
  const Side other = side == Side::A ? Side::B : Side::A;
  SideState& otherState = sides_[static_cast<size_t>(other)];
  if (otherState.paired && macEqual(otherState.mac, mac)) {
    clearPeer(other);
  }

  SideState& node = sides_[static_cast<size_t>(side)];
  const bool same = node.paired && macEqual(node.mac, mac);
  if (node.paired && !same) {
    removeRadioPeer(node.mac);
    cancelInflight(side);
    resetRuntime(node);
  }
  node.paired = true;
  node.role = role;
  node.remote = remoteSide;
  macCopy(node.mac, mac);
  if (fromCable) {
    savePeer(side);
  }
  if (radioOk_) {
    addRadioPeer(mac);
  }
  if (!fromCable) {
    return;
  }

  char peer[18];
  formatMac(mac, peer, sizeof(peer));
  Serial.printf("Paired side %s with %s (remote %s, %s)\n", sideName(side), peer, sideName(remoteSide),
                roleName(role));
  if (onPaired_ != nullptr) {
    onPaired_(side, mac, role, remoteSide);
  }
}

void Link::adoptPeer(Side side, const uint8_t mac[kMacSize], ModuleRole role, Side remoteSide) {
  installPeer(side, mac, role, remoteSide, true);
}

bool Link::addRadioPeer(const uint8_t mac[kMacSize]) {
  if (!radioOk_ || !macValid(mac)) {
    return false;
  }
  if (esp_now_is_peer_exist(mac)) {
    esp_now_del_peer(mac);
  }
  esp_now_peer_info_t peer = {};
  macCopy(peer.peer_addr, mac);
  peer.channel = 0;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

void Link::removeRadioPeer(const uint8_t mac[kMacSize]) {
  if (!radioOk_ || mac == nullptr) {
    return;
  }
  if (esp_now_is_peer_exist(mac)) {
    esp_now_del_peer(mac);
  }
}

void Link::resetRuntime(SideState& node) {
  node.queue = TxQueue{};
  node.online = false;
  node.haveRx = false;
  node.lastRxMs = 0;
  node.lastBoot = 0;
  node.lastSeq = 0;
  node.nextSeq = 1;
}

void Link::cancelInflight(Side side) {
  if (!inflight_.active || inflight_.side != side) {
    return;
  }
  if (radioBusy_ && job_ == RadioJob::Data) {
    dropOne_ = true;
    dropSinceMs_ = millis();
  }
  inflight_.active = false;
  inflight_.phase = Phase::Idle;
}

bool Link::enqueue(Side side, const Frame& frame, uint8_t* seq) {
  SideState* node = state(side);
  if (node == nullptr || node->queue.count >= kTxQueueDepth) {
    return false;
  }
  const uint8_t index = static_cast<uint8_t>((node->queue.head + node->queue.count) % kTxQueueDepth);
  node->queue.items[index] = frame;
  node->queue.count++;
  if (seq != nullptr) {
    *seq = frame.seq;
  }
  return true;
}

bool Link::sideFromMac(const uint8_t mac[kMacSize], Side& side) const {
  for (size_t i = 0; i < 2; ++i) {
    if (sides_[i].paired && macEqual(sides_[i].mac, mac)) {
      side = static_cast<Side>(i);
      return true;
    }
  }
  return false;
}

Link::SideState* Link::state(Side side) {
  if (!validSide(side)) {
    return nullptr;
  }
  return &sides_[static_cast<size_t>(side)];
}

const Link::SideState* Link::state(Side side) const {
  if (!validSide(side)) {
    return nullptr;
  }
  return &sides_[static_cast<size_t>(side)];
}

void Link::handleRx(const uint8_t* mac, const uint8_t* data, int len) {
  if (rxQueue_ == nullptr || mac == nullptr || data == nullptr || len <= 0 || len > 32) {
    return;
  }
  RxEvent event = {};
  macCopy(event.mac, mac);
  event.len = static_cast<uint8_t>(len);
  memcpy(event.data, data, static_cast<size_t>(len));
  xQueueSend(rxQueue_, &event, 0);
}

void Link::onRadioRx(const RxEvent& event) {
  Frame frame;
  if (!decodeFrame(kRadioMagic0, kRadioMagic1, event.data, event.len, frame)) {
    return;
  }
  Side from = Side::A;
  if (!sideFromMac(event.mac, from)) {
    return;
  }
  touch(from);

  if (frame.type == static_cast<uint8_t>(MsgType::Ack)) {
    uint8_t seq = 0;
    uint8_t type = 0;
    uint16_t boot = 0;
    if (!unpackAck(frame, seq, type, boot)) {
      return;
    }
    if (inflight_.active && inflight_.side == from && inflight_.frame.seq == seq && inflight_.frame.type == type &&
        inflight_.frame.bootId == boot) {
      completeDelivery(true);
    }
    return;
  }

  if (frame.type == static_cast<uint8_t>(MsgType::Heartbeat)) {
    return;
  }

  Received message;
  message.from = from;
  message.type = static_cast<MsgType>(frame.type);
  message.seq = frame.seq;
  bool known = false;
  if (frame.type == static_cast<uint8_t>(MsgType::Vorblock) ||
      frame.type == static_cast<uint8_t>(MsgType::Rueckblock) ||
      frame.type == static_cast<uint8_t>(MsgType::Achszahl)) {
    known = unpackAchszahl(frame, message.achszahl);
  } else if (frame.type == static_cast<uint8_t>(MsgType::Fahrtrichtung)) {
    known = unpackFahrtrichtung(frame, message.befehl, message.richtung);
  }
  if (!known) {
    return;
  }

  queueAck(event.mac, frame.seq, frame.type, frame.bootId);

  SideState& node = sides_[static_cast<size_t>(from)];
  const bool duplicate = node.haveRx && node.lastBoot == frame.bootId && node.lastSeq == frame.seq;
  node.haveRx = true;
  node.lastBoot = frame.bootId;
  node.lastSeq = frame.seq;
  if (duplicate || onReceive_ == nullptr) {
    return;
  }
  onReceive_(message);
}

void Link::noteSendFinished(bool ok) {
  if (txQueue_ == nullptr) {
    return;
  }
  TxEvent event = {};
  event.ok = ok ? 1 : 0;
  xQueueSend(txQueue_, &event, 0);
}

void Link::drainResults() {
  if (txQueue_ == nullptr) {
    return;
  }
  TxEvent event = {};
  while (xQueueReceive(txQueue_, &event, 0) == pdTRUE) {
    if (dropOne_) {
      dropOne_ = false;
      continue;
    }
    applyResult(event.ok != 0);
  }
}

void Link::timeoutBusy() {
  if (!radioBusy_) {
    return;
  }
  if ((millis() - radioStartMs_) < kRadioCallbackTimeoutMs) {
    return;
  }
  radioBusy_ = false;
  dropOne_ = true;
  dropSinceMs_ = millis();
  applyResult(false);
}

void Link::applyResult(bool ok) {
  radioBusy_ = false;
  const RadioJob job = job_;
  job_ = RadioJob::None;

  if (job == RadioJob::Ack) {
    if (ackCount_ == 0) {
      return;
    }
    if (ok || acks_[ackHead_].attempts >= 3) {
      popAck();
    } else {
      acks_[ackHead_].attempts++;
    }
    return;
  }
  if (job != RadioJob::Data || !inflight_.active || inflight_.phase != Phase::WaitingMac) {
    return;
  }
  if (ok) {
    inflight_.phase = Phase::WaitingApp;
    inflight_.waitMark = millis();
  } else {
    inflight_.phase = Phase::Retry;
  }
}

void Link::pumpRx() {
  if (rxQueue_ == nullptr) {
    return;
  }
  RxEvent event = {};
  while (xQueueReceive(rxQueue_, &event, 0) == pdTRUE) {
    onRadioRx(event);
  }
}

void Link::pumpRadio() {
  drainResults();
  if (dropOne_) {
    if ((millis() - dropSinceMs_) > 2000) {
      dropOne_ = false;
    } else {
      return;
    }
  }
  timeoutBusy();
  if (dropOne_ || radioBusy_) {
    return;
  }
  if (static_cast<int32_t>(holdoffUntil_ - millis()) > 0) {
    return;
  }
  if (sendNextAck()) {
    return;
  }
  if (serviceInflight()) {
    return;
  }
  if (sendNextData()) {
    return;
  }
  sendHeartbeatIfDue();
}

bool Link::sendNextAck() {
  while (ackCount_ > 0) {
    PendingAck& pending = acks_[ackHead_];
    if (pending.attempts >= 3 || !radioOk_ || !esp_now_is_peer_exist(pending.mac)) {
      popAck();
      continue;
    }
    if (!startSend(pending.mac, pending.frame, RadioJob::Ack)) {
      pending.attempts++;
      holdoffUntil_ = millis() + 20;
      if (pending.attempts >= 3) {
        popAck();
      }
      return true;
    }
    return true;
  }
  return false;
}

bool Link::serviceInflight() {
  if (!inflight_.active) {
    return false;
  }
  // WaitingMac with the radio already idle means the send callback was lost.
  // Drop one late result so it cannot be applied to the retry.
  if (inflight_.phase == Phase::WaitingMac) {
    dropOne_ = true;
    dropSinceMs_ = millis();
    inflight_.phase = Phase::Retry;
    return true;
  }
  if (inflight_.phase == Phase::WaitingApp && (millis() - inflight_.waitMark) < kRadioAckTimeoutMs) {
    return true;
  }
  if (inflight_.attempts >= kMaxSendAttempts) {
    completeDelivery(false);
    return false;
  }
  SideState& node = sides_[static_cast<size_t>(inflight_.side)];
  if (!node.paired) {
    completeDelivery(false);
    return false;
  }
  inflight_.attempts++;
  if (!startSend(node.mac, inflight_.frame, RadioJob::Data)) {
    inflight_.phase = Phase::Retry;
    holdoffUntil_ = millis() + 20;
    return true;
  }
  inflight_.phase = Phase::WaitingMac;
  return true;
}

bool Link::sendNextData() {
  if (inflight_.active) {
    return false;
  }
  for (int n = 0; n < 2; ++n) {
    const uint8_t index = static_cast<uint8_t>((rr_ + n) % 2);
    SideState& node = sides_[index];
    if (!node.paired || node.queue.count == 0) {
      continue;
    }
    Frame frame = node.queue.items[node.queue.head];
    node.queue.head = static_cast<uint8_t>((node.queue.head + 1) % kTxQueueDepth);
    node.queue.count--;

    inflight_.active = true;
    inflight_.phase = Phase::WaitingMac;
    inflight_.side = static_cast<Side>(index);
    inflight_.attempts = 1;
    inflight_.frame = frame;
    rr_ = static_cast<uint8_t>((index + 1) % 2);
    if (!startSend(node.mac, frame, RadioJob::Data)) {
      inflight_.phase = Phase::Retry;
      holdoffUntil_ = millis() + 20;
    }
    return true;
  }
  return false;
}

void Link::sendHeartbeatIfDue() {
  const uint32_t now = millis();
  for (int n = 0; n < 2; ++n) {
    const uint8_t index = static_cast<uint8_t>((hbCursor_ + n) % 2);
    SideState& node = sides_[index];
    if (!node.paired || (now - lastHeartbeatMs_[index]) < kHeartbeatIntervalMs) {
      continue;
    }
    Frame beat = {};
    beat.type = static_cast<uint8_t>(MsgType::Heartbeat);
    beat.bootId = bootId_;
    lastHeartbeatMs_[index] = now;
    hbCursor_ = static_cast<uint8_t>((index + 1) % 2);
    if (!startSend(node.mac, beat, RadioJob::Heartbeat)) {
      holdoffUntil_ = now + 20;
    }
    return;
  }
}

bool Link::startSend(const uint8_t mac[kMacSize], const Frame& frame, RadioJob job) {
  if (!radioOk_) {
    return false;
  }
  uint8_t buf[kFrameSize];
  if (encodeFrame(kRadioMagic0, kRadioMagic1, frame, buf, sizeof(buf)) != kFrameSize) {
    return false;
  }
  job_ = job;
  radioBusy_ = true;
  radioStartMs_ = millis();
  const esp_err_t err = esp_now_send(mac, buf, kFrameSize);
  if (err != ESP_OK) {
    radioBusy_ = false;
    job_ = RadioJob::None;
    return false;
  }
  return true;
}

void Link::queueAck(const uint8_t mac[kMacSize], uint8_t seq, uint8_t type, uint16_t boot) {
  if (ackCount_ >= kAckQueueDepth) {
    return;
  }
  const uint8_t index = static_cast<uint8_t>((ackHead_ + ackCount_) % kAckQueueDepth);
  PendingAck& pending = acks_[index];
  pending = PendingAck{};
  macCopy(pending.mac, mac);
  packAck(pending.frame, bootId_, seq, type, boot);
  ackCount_++;
}

void Link::popAck() {
  if (ackCount_ == 0) {
    return;
  }
  ackHead_ = static_cast<uint8_t>((ackHead_ + 1) % kAckQueueDepth);
  ackCount_--;
}

void Link::completeDelivery(bool ok) {
  const Side side = inflight_.side;
  const uint8_t type = inflight_.frame.type;
  const uint8_t seq = inflight_.frame.seq;
  inflight_.active = false;
  inflight_.phase = Phase::Idle;
  if (!ok) {
    Serial.printf("Send failed side %s %s seq %u\n", sideName(side), msgTypeName(type), seq);
  }
  if (onDelivery_ != nullptr) {
    onDelivery_(side, static_cast<MsgType>(type), seq, ok);
  }
}

void Link::touch(Side side) {
  SideState& node = sides_[static_cast<size_t>(side)];
  node.lastRxMs = millis();
  if (!node.online) {
    node.online = true;
    Serial.printf("Side %s online\n", sideName(side));
  }
}

void Link::refreshOnline() {
  const uint32_t now = millis();
  for (size_t i = 0; i < 2; ++i) {
    SideState& node = sides_[i];
    if (!node.paired) {
      node.online = false;
      continue;
    }
    const bool on = node.lastRxMs != 0 && (now - node.lastRxMs) < kOnlineTimeoutMs;
    if (node.online && !on) {
      node.online = false;
      Serial.printf("Side %s offline\n", sideName(static_cast<Side>(i)));
    }
  }
}

uint8_t Link::allocSeq(SideState& node) {
  uint8_t seq = node.nextSeq == 0 ? 1 : node.nextSeq;
  node.nextSeq = seq == 255 ? 1 : static_cast<uint8_t>(seq + 1);
  return seq;
}

void Link::discoverThunk(Side side, const uint8_t mac[kMacSize], ModuleRole role, Side remoteSide) {
  instance().adoptPeer(side, mac, role, remoteSide);
}

}  // namespace bahn
