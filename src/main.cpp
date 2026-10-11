#include <Arduino.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axle_counter.h"
#include "axle_sensor.h"
#include "config.h"
#include "link.h"

namespace {

uint32_t flashUntilMs = 0;
bool buttonDown = false;
bool buttonArmed = false;
bool longHandled = false;
uint32_t pressStartedMs = 0;
char line[72];
size_t lineLen = 0;
bahn::AxleSensor axleSensor;
bahn::AxleCounter axleCounter;

void flushAxle() {
  bahn::Link& link = bahn::Link::instance();
  const bahn::Side sides[2] = {bahn::Side::A, bahn::Side::B};
  for (size_t i = 0; i < 2; ++i) {
    const bahn::Side side = sides[i];
    uint16_t count = 0;
    if (axleCounter.takeVorblock(side, count) && !link.sendVorblock(side, count)) {
      axleCounter.onSendFailed(side, bahn::MsgType::Vorblock);
    }
    if (axleCounter.takeAchszahl(side, count) && !link.sendAchszahl(side, count)) {
      axleCounter.onSendFailed(side, bahn::MsgType::Achszahl);
    }
    if (axleCounter.takeRueckblock(side, count) && !link.sendRueckblock(side, count)) {
      axleCounter.onSendFailed(side, bahn::MsgType::Rueckblock);
    }
  }
}

void printAxle() {
  const int16_t net = axleCounter.net();
  if (net > 0) {
    Serial.printf("Axle count %d toward B, block occupied\n", net);
  } else if (net < 0) {
    Serial.printf("Axle count %d toward A, block occupied\n", -net);
  } else {
    Serial.println("Axle count 0, block clear");
  }
  const bahn::Side sides[2] = {bahn::Side::A, bahn::Side::B};
  for (size_t i = 0; i < 2; ++i) {
    uint16_t expected = 0;
    if (axleCounter.neighborExpected(sides[i], expected)) {
      Serial.printf("Neighbor %s announced %u axles%s\n", bahn::sideName(sides[i]), expected,
                    axleCounter.approachFrom(sides[i]) ? ", Vorblock" : "");
    }
  }
}

void onPaired(bahn::Side side, const uint8_t mac[bahn::kMacSize], bahn::ModuleRole role, bahn::Side remote) {
  (void)side;
  (void)mac;
  (void)role;
  (void)remote;
  flashUntilMs = millis() + 1600;
}

void onDelivery(bahn::Side to, bahn::MsgType type, uint8_t seq, bool ok) {
  Serial.printf("tx side %s %s seq %u %s\n", bahn::sideName(to), bahn::msgTypeName(static_cast<uint8_t>(type)), seq,
                ok ? "ok" : "failed");
  if (!ok) {
    axleCounter.onSendFailed(to, type);
    flushAxle();
  }
}

void onReceive(const bahn::Received& message) {
  if (message.type == bahn::MsgType::Vorblock || message.type == bahn::MsgType::Achszahl ||
      message.type == bahn::MsgType::Rueckblock) {
    axleCounter.onRemote(message.from, message.type, message.achszahl, millis());
  }
  if (message.type == bahn::MsgType::Fahrtrichtung) {
    Serial.printf("rx side %s fahrtrichtung seq %u befehl %u richtung %u\n", bahn::sideName(message.from), message.seq,
                  static_cast<unsigned>(message.befehl), message.richtung);
    return;
  }
  Serial.printf("rx side %s %s seq %u achszahl %u\n", bahn::sideName(message.from),
                bahn::msgTypeName(static_cast<uint8_t>(message.type)), message.seq, message.achszahl);
}

void printHelp() {
  Serial.println("Pairing cable: pin1 GND, pin2 TX, pin3 RX. The cable crosses TX and RX.");
  Serial.println("  Side A  TX GPIO27  RX GPIO14");
  Serial.println("  Side B  TX GPIO26  RX GPIO25");
  Serial.println("Plug the cable into the socket facing that neighbor. Unplug it after pairing.");
  Serial.println("Button: short press clears side A, long press clears side B.");
  Serial.println("Commands:");
  Serial.println("  status");
  Serial.println("  clear a|b");
  Serial.println("  vor a|b <axles>");
  Serial.println("  rueck a|b <axles>");
  Serial.println("  achs a|b <axles>");
  Serial.println("  richtung a|b anfragen|festlegen|freigeben|status [0|1]");
  Serial.println("Axle contacts close to GND:  A GPIO32,  B GPIO33.");
  Serial.println("  rad a|b");
}

bool parseSide(const char* text, bahn::Side& side) {
  if (text == nullptr) {
    return false;
  }
  if ((text[0] == 'a' || text[0] == 'A') && text[1] == '\0') {
    side = bahn::Side::A;
    return true;
  }
  if ((text[0] == 'b' || text[0] == 'B') && text[1] == '\0') {
    side = bahn::Side::B;
    return true;
  }
  return false;
}

bool parseCount(const char* text, uint16_t& count) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  const unsigned long value = strtoul(text, &end, 10);
  if (end == text || *end != '\0' || value > 65535UL) {
    return false;
  }
  count = static_cast<uint16_t>(value);
  return true;
}

void lowerInPlace(char* text) {
  for (; text != nullptr && *text != '\0'; ++text) {
    if (*text >= 'A' && *text <= 'Z') {
      *text = static_cast<char>(*text - 'A' + 'a');
    }
  }
}

void clearSide(bahn::Side side) {
  bahn::Link::instance().clearPeer(side);
  Serial.printf("Side %s cleared. Connect the pairing cable to pair again.\n", bahn::sideName(side));
}

void handleLine(char* text) {
  char cmd[16] = {};
  char arg1[16] = {};
  char arg2[16] = {};
  char arg3[16] = {};
  const int count = sscanf(text, "%15s %15s %15s %15s", cmd, arg1, arg2, arg3);
  if (count <= 0) {
    return;
  }
  lowerInPlace(cmd);
  lowerInPlace(arg1);
  lowerInPlace(arg2);

  bahn::Link& link = bahn::Link::instance();
  if (strcmp(cmd, "help") == 0) {
    printHelp();
    return;
  }
  if (strcmp(cmd, "status") == 0) {
    link.logStatus();
    printAxle();
    return;
  }
  if (strcmp(cmd, "rad") == 0) {
    bahn::Side side = bahn::Side::A;
    if (!parseSide(arg1, side)) {
      Serial.println("Side must be a or b");
      return;
    }
    const uint32_t now = millis();
    const bahn::AxleDirection direction = side == bahn::Side::B ? bahn::AxleDirection::TowardB : bahn::AxleDirection::TowardA;
    axleCounter.onPassage(direction, now);
    axleCounter.update(now);
    flushAxle();
    Serial.printf("Axle toward %s, count %d\n", bahn::sideName(side), axleCounter.net());
    return;
  }

  bahn::Side side = bahn::Side::A;
  if ((strcmp(cmd, "clear") == 0 || strcmp(cmd, "vor") == 0 || strcmp(cmd, "rueck") == 0 || strcmp(cmd, "achs") == 0 ||
       strcmp(cmd, "richtung") == 0) &&
      !parseSide(arg1, side)) {
    Serial.println("Side must be a or b");
    return;
  }

  if (strcmp(cmd, "clear") == 0) {
    clearSide(side);
    return;
  }

  uint16_t axles = 0;
  if (strcmp(cmd, "vor") == 0 || strcmp(cmd, "rueck") == 0 || strcmp(cmd, "achs") == 0) {
    if (!parseCount(arg2, axles)) {
      Serial.println("Axle count missing");
      return;
    }
  }

  bool queued = false;
  if (strcmp(cmd, "vor") == 0) {
    queued = link.sendVorblock(side, axles);
  } else if (strcmp(cmd, "rueck") == 0) {
    queued = link.sendRueckblock(side, axles);
  } else if (strcmp(cmd, "achs") == 0) {
    queued = link.sendAchszahl(side, axles);
  } else if (strcmp(cmd, "richtung") == 0) {
    bahn::FahrtrichtungBefehl befehl = bahn::FahrtrichtungBefehl::Status;
    if (strcmp(arg2, "anfragen") == 0) {
      befehl = bahn::FahrtrichtungBefehl::Anfragen;
    } else if (strcmp(arg2, "festlegen") == 0) {
      befehl = bahn::FahrtrichtungBefehl::Festlegen;
    } else if (strcmp(arg2, "freigeben") == 0) {
      befehl = bahn::FahrtrichtungBefehl::Freigeben;
    } else if (strcmp(arg2, "status") == 0) {
      befehl = bahn::FahrtrichtungBefehl::Status;
    } else {
      Serial.println("Befehl must be anfragen, festlegen, freigeben or status");
      return;
    }
    uint16_t richtung = 0;
    if (arg3[0] != '\0' && !parseCount(arg3, richtung)) {
      Serial.println("Richtung must be a number");
      return;
    }
    queued = link.sendFahrtrichtung(side, befehl, static_cast<uint8_t>(richtung));
  } else {
    Serial.println("Unknown command. Type help.");
    return;
  }

  if (!queued) {
    Serial.println("Not sent. Side is not paired or the queue is full.");
  }
}

void pollSerial() {
  while (Serial.available() > 0) {
    const int raw = Serial.read();
    if (raw < 0) {
      return;
    }
    const char ch = static_cast<char>(raw);
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      line[lineLen] = '\0';
      handleLine(line);
      lineLen = 0;
      continue;
    }
    if (lineLen + 1 < sizeof(line)) {
      line[lineLen++] = ch;
    }
  }
}

void pollButton() {
  const bool down = digitalRead(bahn::kButtonPin) == LOW;
  if (!buttonArmed) {
    if (!down) {
      buttonArmed = true;
    }
    return;
  }
  const uint32_t now = millis();
  if (down && !buttonDown) {
    buttonDown = true;
    longHandled = false;
    pressStartedMs = now;
  }
  if (buttonDown && down && !longHandled && (now - pressStartedMs) >= 700) {
    longHandled = true;
    clearSide(bahn::Side::B);
  }
  if (buttonDown && !down) {
    if (!longHandled && (now - pressStartedMs) >= 40) {
      clearSide(bahn::Side::A);
    }
    buttonDown = false;
  }
}

void pollLed() {
  bahn::Link& link = bahn::Link::instance();
  const bool pairedA = link.paired(bahn::Side::A);
  const bool pairedB = link.paired(bahn::Side::B);
  const bool anyPaired = pairedA || pairedB;
  const bool allOnline = (!pairedA || link.online(bahn::Side::A)) && (!pairedB || link.online(bahn::Side::B));
  const uint32_t now = millis();

  bool on = false;
  if (anyPaired && allOnline && now >= flashUntilMs) {
    on = true;
  } else {
    const uint32_t period = now < flashUntilMs ? 80 : (anyPaired ? 250 : 800);
    on = ((now / period) % 2U) == 0U;
  }
  digitalWrite(bahn::kLedPin, on ? HIGH : LOW);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(bahn::kLedPin, OUTPUT);
  pinMode(bahn::kButtonPin, INPUT_PULLUP);
  pinMode(bahn::kAxleAPin, INPUT_PULLUP);
  pinMode(bahn::kAxleBPin, INPUT_PULLUP);
  digitalWrite(bahn::kLedPin, LOW);

  bahn::Link& link = bahn::Link::instance();
  link.onReceive(onReceive);
  link.onDelivery(onDelivery);
  link.onPaired(onPaired);
  link.begin(bahn::ModuleRole::Block);
  printHelp();
}

void pollAxles() {
  const uint32_t now = millis();
  const bool contactA = digitalRead(bahn::kAxleAPin) == LOW;
  const bool contactB = digitalRead(bahn::kAxleBPin) == LOW;
  const bahn::AxleDirection direction = axleSensor.poll(contactA, contactB, now);
  if (direction != bahn::AxleDirection::None) {
    axleCounter.onPassage(direction, now);
    Serial.printf("Axle toward %s, count %d\n", bahn::axleDirectionName(direction), axleCounter.net());
  }
  axleCounter.update(now);
  flushAxle();
}

void loop() {
  bahn::Link::instance().loop();
  pollAxles();
  pollSerial();
  pollButton();
  pollLed();
}
