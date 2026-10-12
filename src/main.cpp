#include <Arduino.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axle_counter.h"
#include "axle_sensor.h"
#include "bahnhof.h"
#include "config.h"
#include "link.h"
#include "matrix.h"
#include "oled.h"
#include "weichen_web.h"

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
bahn::Oled oled;

#if MODULE_BAHNHOF
bahn::Bahnhof bahnhof;
bahn::Matrix matrix;
bool festlegenPending = false;
bool freigebenPending = false;
bool festlegenInflight = false;
bool freigebenInflight = false;
uint8_t richtungSeq = 0;

struct Taste {
  bool level = false;
  bool stable = false;
  uint32_t changedMs = 0;

  void update(bool down, uint32_t now) {
    if (down != level) {
      level = down;
      changedMs = now;
    }
    if ((now - changedMs) >= bahn::kTasteDebounceMs) {
      stable = level;
    }
  }
};

Taste gleisTaste[3];
Taste streckeTaste;
Taste einfahrtTaste;
bool ausCombo = false;
bool einCombo = false;
#else
bahn::RichtungView blockRichtung = bahn::RichtungView::Keine;
#endif

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

#if MODULE_BAHNHOF
bool streckeBelegt() {
  uint16_t announced = 0;
  return axleCounter.occupied(bahn::kStreckeSeite) || axleCounter.approachFrom(bahn::kStreckeSeite) ||
         axleCounter.neighborExpected(bahn::kStreckeSeite, announced);
}

void flushRichtung() {
  if (festlegenInflight || freigebenInflight) {
    return;
  }
  bahn::Link& link = bahn::Link::instance();
  uint8_t seq = 0;
  if (festlegenPending) {
    if (link.sendFahrtrichtung(bahn::kStreckeSeite, bahn::FahrtrichtungBefehl::Festlegen, bahn::kRichtungAusfahrt, &seq)) {
      festlegenInflight = true;
      richtungSeq = seq;
    }
  } else if (freigebenPending) {
    if (link.sendFahrtrichtung(bahn::kStreckeSeite, bahn::FahrtrichtungBefehl::Freigeben, 0, &seq)) {
      freigebenInflight = true;
      richtungSeq = seq;
    }
  }
}

void reportBahnhof(const bahn::BahnhofEvent& event) {
  switch (event.ergebnis) {
    case bahn::BahnErgebnis::Festgelegt:
      Serial.printf("Exit signal Vorblock, track %u%s\n", bahnhof.gleis(),
                    event.festlegen ? ", line locked outbound" : "");
      break;
    case bahn::BahnErgebnis::AbgelehntGegenrichtung:
      Serial.println("Line request refused: opposite direction is set");
      break;
    case bahn::BahnErgebnis::AbgelehntZug:
      Serial.println("Line request refused: train on the line");
      break;
    case bahn::BahnErgebnis::EinfahrtGestellt:
      Serial.printf("Entry signal cleared, track %u\n", bahnhof.gleis());
      break;
    case bahn::BahnErgebnis::AbgelehntKeineEinfahrt:
      Serial.println("Entry signal refused: line is not set to entry");
      break;
    case bahn::BahnErgebnis::AbgelehntGleis:
      Serial.println("Route refused: track is switched off");
      break;
    case bahn::BahnErgebnis::Freigegeben:
      Serial.println("Line released");
      break;
    case bahn::BahnErgebnis::None:
      break;
  }
}

void stelleWeichen() {
  int8_t lage[bahn::kWeichenAnzahl];
  bahnhof.weichen(lage);
  const int pins[bahn::kWeichenAnzahl] = {bahn::kWeiche1Pin, bahn::kWeiche2Pin, bahn::kWeiche3Pin, bahn::kWeiche4Pin};
  for (uint8_t i = 0; i < bahn::kWeichenAnzahl; ++i) {
    if (lage[i] < 0) {
      continue;
    }
    digitalWrite(pins[i], lage[i] == 0 ? LOW : HIGH);
  }
  bahnhof.signaleStellen();
}

void applyBahnhof(const bahn::BahnhofEvent& event) {
  if (event.ergebnis == bahn::BahnErgebnis::Festgelegt || event.ergebnis == bahn::BahnErgebnis::EinfahrtGestellt) {
    stelleWeichen();
  }
  if (event.festlegen) {
    freigebenPending = false;
    freigebenInflight = false;
    festlegenPending = true;
  }
  if (event.freigeben) {
    festlegenPending = false;
    festlegenInflight = false;
    freigebenPending = true;
  }
  flushRichtung();
  reportBahnhof(event);
}

void printBahnhof() {
  const char* richtung = "keine";
  if (bahnhof.richtung() == bahn::Linienrichtung::Ausfahrt) {
    richtung = "Ausfahrt";
  } else if (bahnhof.richtung() == bahn::Linienrichtung::Einfahrt) {
    richtung = "Einfahrt";
  }
  Serial.printf("Station %s, exit %s, entry %s, track %u\n", richtung, bahn::begriffName(bahnhof.ausfahrtSignal()),
                bahn::begriffName(bahnhof.einfahrtSignal()), bahnhof.gleis());
}

void pollBahnhof() {
  const uint32_t now = millis();
  gleisTaste[0].update(digitalRead(bahn::kGleis1Pin) == LOW, now);
  gleisTaste[1].update(digitalRead(bahn::kGleis2Pin) == LOW, now);
  gleisTaste[2].update(digitalRead(bahn::kGleis3Pin) == LOW, now);
  streckeTaste.update(digitalRead(bahn::kStreckePin) == LOW, now);
  einfahrtTaste.update(digitalRead(bahn::kEinfahrtPin) == LOW, now);

  uint8_t gleis = 0;
  uint8_t pressed = 0;
  for (uint8_t i = 0; i < 3; ++i) {
    if (gleisTaste[i].stable && bahnhof.gleisAn(static_cast<uint8_t>(i + 1))) {
      gleis = static_cast<uint8_t>(i + 1);
      pressed++;
    }
  }
  const bool oneTrack = pressed == 1;
  const bool aus = oneTrack && streckeTaste.stable && !einfahrtTaste.stable;
  const bool ein = oneTrack && einfahrtTaste.stable && !streckeTaste.stable;
  if (aus && !ausCombo) {
    applyBahnhof(bahnhof.ausfahrt(gleis, streckeBelegt()));
  }
  if (ein && !einCombo) {
    applyBahnhof(bahnhof.einfahrt(gleis));
  }
  ausCombo = aus;
  einCombo = ein;
  applyBahnhof(bahnhof.poll(streckeBelegt()));
  flushRichtung();
}

void pollMatrix() {
  uint8_t ausfahrt = 0;
  uint8_t einfahrt = 0;
  bahnhof.anzeige(ausfahrt, einfahrt);
  matrix.show(ausfahrt, einfahrt);
}
#endif

void noteAxle(bahn::AxleDirection direction) {
#if MODULE_BAHNHOF
  const bool towardLine = (bahn::kStreckeSeite == bahn::Side::B && direction == bahn::AxleDirection::TowardB) ||
                          (bahn::kStreckeSeite == bahn::Side::A && direction == bahn::AxleDirection::TowardA);
  bahnhof.onAxle(towardLine);
#else
  (void)direction;
#endif
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
#if MODULE_BAHNHOF
  if (type == bahn::MsgType::Fahrtrichtung && to == bahn::kStreckeSeite &&
      ((festlegenInflight && seq == richtungSeq) || (freigebenInflight && seq == richtungSeq))) {
    const bool wasFestlegen = festlegenInflight;
    festlegenInflight = false;
    freigebenInflight = false;
    if (ok) {
      if (wasFestlegen) {
        festlegenPending = false;
      } else {
        freigebenPending = false;
      }
    }
    flushRichtung();
  }
#endif
}

void onReceive(const bahn::Received& message) {
  if (message.type == bahn::MsgType::Vorblock || message.type == bahn::MsgType::Achszahl ||
      message.type == bahn::MsgType::Rueckblock) {
    axleCounter.onRemote(message.from, message.type, message.achszahl, millis());
  }
  if (message.type == bahn::MsgType::Fahrtrichtung) {
    Serial.printf("rx side %s fahrtrichtung seq %u befehl %u richtung %u\n", bahn::sideName(message.from), message.seq,
                  static_cast<unsigned>(message.befehl), message.richtung);
#if MODULE_BAHNHOF
    if (message.from == bahn::kStreckeSeite && message.befehl == bahn::FahrtrichtungBefehl::Festlegen &&
        message.richtung == bahn::kRichtungAusfahrt) {
      const bahn::Linienrichtung before = bahnhof.richtung();
      bahnhof.onFestlegen();
      if (before != bahn::Linienrichtung::Ausfahrt && bahnhof.richtung() == bahn::Linienrichtung::Einfahrt) {
        Serial.println("Line set to entry");
      }
    } else if (message.from == bahn::kStreckeSeite && message.befehl == bahn::FahrtrichtungBefehl::Freigeben) {
      const bahn::Linienrichtung before = bahnhof.richtung();
      bahnhof.onFreigeben();
      if (before == bahn::Linienrichtung::Einfahrt) {
        Serial.println("Entry direction released");
      }
    }
#else
    if (message.befehl == bahn::FahrtrichtungBefehl::Festlegen && message.richtung == bahn::kRichtungAusfahrt) {
      blockRichtung = message.from == bahn::Side::A ? bahn::RichtungView::NachB : bahn::RichtungView::NachA;
    } else if (message.befehl == bahn::FahrtrichtungBefehl::Freigeben) {
      blockRichtung = bahn::RichtungView::Keine;
    }
    const bahn::Side other = message.from == bahn::Side::A ? bahn::Side::B : bahn::Side::A;
    if (!bahn::Link::instance().sendFahrtrichtung(other, message.befehl, message.richtung)) {
      Serial.printf("Direction not forwarded to %s\n", bahn::sideName(other));
    }
#endif
    return;
  }
  Serial.printf("rx side %s %s seq %u achszahl %u\n", bahn::sideName(message.from),
                bahn::msgTypeName(static_cast<uint8_t>(message.type)), message.seq, message.achszahl);
}

void printHelp() {
  Serial.println("Pairing cable: pin1 GND, pin2 TX, pin3 RX. The cable crosses TX and RX.");
#if MODULE_BAHNHOF
  Serial.println("  Side B (line)  TX GPIO17  RX GPIO34, 10k to 3.3V");
  Serial.println("  No side A socket on the station.");
#else
  Serial.println("  Side A  TX GPIO27  RX GPIO14");
  Serial.println("  Side B  TX GPIO26  RX GPIO25");
#endif
  Serial.println("Plug the cable into the socket facing that neighbor. Unplug it after pairing.");
  Serial.println("Button: short press clears side A, long press clears side B.");
  Serial.println("Commands:");
  Serial.println("  status");
  Serial.println("  clear a|b");
  Serial.println("  vor a|b <axles>");
  Serial.println("  rueck a|b <axles>");
  Serial.println("  achs a|b <axles>");
  Serial.println("  richtung a|b anfragen|festlegen|freigeben|status [0|1]");
#if MODULE_BAHNHOF
  Serial.println("Axle contacts close to GND, 10k to 3.3V:  A GPIO35,  B GPIO36.");
#else
  Serial.println("Axle contacts close to GND:  A GPIO32,  B GPIO33.");
#endif
  Serial.println("OLED SSD1306 128x64: SDA GPIO21, SCL GPIO22, 3.3V.");
  Serial.println("  rad a|b");
#if MODULE_BAHNHOF
  Serial.println("Station keys, active low to GND:");
  Serial.println("  Track 1 GPIO4, track 2 GPIO39 (10k to 3.3V), track 3 GPIO18");
  Serial.println("  Entry signal GPIO19, line GPIO5");
  Serial.println("  aus 1|2|3     track key plus line key");
  Serial.println("  ein 1|2|3     entry key plus track key");
  Serial.println("MAX7219 chain, 5V, shared GND:");
  Serial.println("  DIN GPIO2, CLK GPIO15, CS GPIO16");
  Serial.println("  Display 1 departure, display 2 from its DOUT is entry");
  Serial.printf("Turnout page on WLAN %s, password %s, http://192.168.4.1/\n", bahn::kWebSsid, bahn::kWebPass);
#endif
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
#if MODULE_BAHNHOF
    printBahnhof();
#else
    const char* richtung = "keine";
    if (blockRichtung == bahn::RichtungView::NachA) {
      richtung = "nach A";
    } else if (blockRichtung == bahn::RichtungView::NachB) {
      richtung = "nach B";
    }
    Serial.printf("Block direction %s\n", richtung);
#endif
    return;
  }
#if MODULE_BAHNHOF
  if (strcmp(cmd, "aus") == 0 || strcmp(cmd, "ein") == 0) {
    uint16_t gleis = 0;
    if (!parseCount(arg1, gleis) || gleis < 1 || gleis > 3) {
      Serial.println("Track must be 1, 2 or 3");
      return;
    }
    if (strcmp(cmd, "aus") == 0) {
      applyBahnhof(bahnhof.ausfahrt(static_cast<uint8_t>(gleis), streckeBelegt()));
    } else {
      applyBahnhof(bahnhof.einfahrt(static_cast<uint8_t>(gleis)));
    }
    return;
  }
#endif
  if (strcmp(cmd, "rad") == 0) {
    bahn::Side side = bahn::Side::A;
    if (!parseSide(arg1, side)) {
      Serial.println("Side must be a or b");
      return;
    }
    const uint32_t now = millis();
    const bahn::AxleDirection direction = side == bahn::Side::B ? bahn::AxleDirection::TowardB : bahn::AxleDirection::TowardA;
    axleCounter.onPassage(direction, now);
    noteAxle(direction);
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

void armInput(int pin) {
  pinMode(pin, (pin >= 34 && pin <= 39) ? INPUT : INPUT_PULLUP);
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
  armInput(bahn::kAxleAPin);
  armInput(bahn::kAxleBPin);
#if MODULE_BAHNHOF
  armInput(bahn::kGleis1Pin);
  armInput(bahn::kGleis2Pin);
  armInput(bahn::kGleis3Pin);
  armInput(bahn::kEinfahrtPin);
  armInput(bahn::kStreckePin);
  const int weichen[bahn::kWeichenAnzahl] = {bahn::kWeiche1Pin, bahn::kWeiche2Pin, bahn::kWeiche3Pin, bahn::kWeiche4Pin};
  for (uint8_t i = 0; i < bahn::kWeichenAnzahl; ++i) {
    pinMode(weichen[i], OUTPUT);
    digitalWrite(weichen[i], LOW);
  }
  const int lampen[] = {bahn::kAusfahrtFreiPin, bahn::kAusfahrtGelbPin, bahn::kEinfahrtFreiPin, bahn::kEinfahrtGelbPin};
  for (int pin : lampen) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  matrix.begin();
  bahn::weichenLaden(bahnhof);
#endif
  digitalWrite(bahn::kLedPin, LOW);

  bahn::Link& link = bahn::Link::instance();
  link.onReceive(onReceive);
  link.onDelivery(onDelivery);
  link.onPaired(onPaired);
  link.begin(bahn::kModuleRole);
#if MODULE_BAHNHOF
  bahn::weichenWebStarten(bahnhof);
#endif
  oled.begin();
  printHelp();
}

void pollAxles() {
  const uint32_t now = millis();
  const bool contactA = digitalRead(bahn::kAxleAPin) == LOW;
  const bool contactB = digitalRead(bahn::kAxleBPin) == LOW;
  const bahn::AxleDirection direction = axleSensor.poll(contactA, contactB, now);
  if (direction != bahn::AxleDirection::None) {
    axleCounter.onPassage(direction, now);
    noteAxle(direction);
    Serial.printf("Axle toward %s, count %d\n", bahn::axleDirectionName(direction), axleCounter.net());
  }
  axleCounter.update(now);
  flushAxle();
}

#if MODULE_BAHNHOF
bahn::SignalView signalView(bahn::Begriff begriff) {
  if (begriff == bahn::Begriff::Frei) {
    return bahn::SignalView::Frei;
  }
  if (begriff == bahn::Begriff::Frei40) {
    return bahn::SignalView::Frei40;
  }
  return bahn::SignalView::Halt;
}

void stelleSignal(int freiPin, int gelbPin, bahn::Begriff begriff) {
  digitalWrite(freiPin, begriff == bahn::Begriff::Halt ? LOW : HIGH);
  digitalWrite(gelbPin, begriff == bahn::Begriff::Frei40 ? HIGH : LOW);
}

void stelleSignale() {
  stelleSignal(bahn::kAusfahrtFreiPin, bahn::kAusfahrtGelbPin, bahnhof.ausfahrtSignal());
  stelleSignal(bahn::kEinfahrtFreiPin, bahn::kEinfahrtGelbPin, bahnhof.einfahrtSignal());
}
#endif

void pollOled() {
  bahn::Link& link = bahn::Link::instance();
  bahn::ModuleView view;
  view.pairA = bahn::pairView(link.paired(bahn::Side::A), link.online(bahn::Side::A));
  view.pairB = bahn::pairView(link.paired(bahn::Side::B), link.online(bahn::Side::B));
#if MODULE_BAHNHOF
  view.bahnhof = true;
  view.signalA = signalView(bahnhof.ausfahrtSignal());
  view.signalB = signalView(bahnhof.einfahrtSignal());
  if (bahnhof.richtung() == bahn::Linienrichtung::Ausfahrt) {
    view.richtung = bahn::RichtungView::Ausfahrt;
  } else if (bahnhof.richtung() == bahn::Linienrichtung::Einfahrt) {
    view.richtung = bahn::RichtungView::Einfahrt;
  }
#else
  view.richtung = blockRichtung;
#endif
  view.axleNet = axleCounter.net();
  oled.show(view);
}

void loop() {
  bahn::Link::instance().loop();
  pollAxles();
#if MODULE_BAHNHOF
  pollBahnhof();
  stelleSignale();
  pollMatrix();
  bahn::weichenWebLoop();
#endif
  pollOled();
  pollSerial();
  pollButton();
  pollLed();
}
