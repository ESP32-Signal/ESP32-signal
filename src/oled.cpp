#include "oled.h"

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

#include <stdio.h>

#include "config.h"

namespace bahn {
namespace {

U8G2_SSD1306_128X64_NONAME_F_HW_I2C& screen() {
  static U8G2_SSD1306_128X64_NONAME_F_HW_I2C device(U8G2_R0, U8X8_PIN_NONE, kOledSclPin, kOledSdaPin);
  return device;
}

const char* pairText(PairView state) {
  switch (state) {
    case PairView::Aktiv:
      return "aktiv";
    case PairView::Still:
      return "still";
    case PairView::Nein:
      break;
  }
  return "nein";
}

const char* signalText(SignalView aspect) {
  switch (aspect) {
    case SignalView::Frei:
      return "Frei";
    case SignalView::Frei40:
      return "F40";
    case SignalView::Halt:
      break;
  }
  return "Halt";
}

bool sameView(const ModuleView& a, const ModuleView& b) {
  return a.bahnhof == b.bahnhof && a.pairA == b.pairA && a.pairB == b.pairB && a.signalA == b.signalA &&
         a.signalB == b.signalB && a.richtung == b.richtung && a.axleNet == b.axleNet;
}

void draw(const ModuleView& view) {
  char line[24];
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C& display = screen();
  display.clearBuffer();
  display.setFont(u8g2_font_7x13_tf);

  snprintf(line, sizeof(line), "A:%-5s B:%s", pairText(view.pairA), pairText(view.pairB));
  display.drawStr(0, 14, line);

  if (view.bahnhof) {
    snprintf(line, sizeof(line), "Aus %-4s Ein %s", signalText(view.signalA), signalText(view.signalB));
  } else {
    snprintf(line, sizeof(line), "S A %-4s B %s", signalText(view.signalA), signalText(view.signalB));
  }
  display.drawStr(0, 30, line);

  const char* richtung = "keine";
  if (view.richtung == RichtungView::NachA) {
    richtung = "nach A";
  } else if (view.richtung == RichtungView::NachB) {
    richtung = "nach B";
  } else if (view.richtung == RichtungView::Ausfahrt) {
    richtung = "Ausfahrt";
  } else if (view.richtung == RichtungView::Einfahrt) {
    richtung = "Einfahrt";
  }
  snprintf(line, sizeof(line), "Ri %s", richtung);
  display.drawStr(0, 46, line);

  if (view.axleNet > 0) {
    snprintf(line, sizeof(line), "Ax %d nach B", view.axleNet);
  } else if (view.axleNet < 0) {
    snprintf(line, sizeof(line), "Ax %d nach A", -view.axleNet);
  } else {
    snprintf(line, sizeof(line), "Ax 0 frei");
  }
  display.drawStr(0, 62, line);
  display.sendBuffer();
}

}  // namespace

PairView pairView(bool paired, bool online) {
  if (!paired) {
    return PairView::Nein;
  }
  return online ? PairView::Aktiv : PairView::Still;
}

bool Oled::begin() {
  Wire.begin(kOledSdaPin, kOledSclPin);
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C& display = screen();
  display.setI2CAddress(static_cast<uint8_t>(kOledI2CAddress << 1));
  display.setBusClock(400000);
  Wire.beginTransmission(kOledI2CAddress);
  if (Wire.endTransmission() != 0) {
    Serial.println("OLED not found");
    ready_ = false;
    return false;
  }
  display.begin();
  ready_ = true;
  shown_ = false;
  Serial.println("OLED ready");
  return true;
}

void Oled::show(const ModuleView& view) {
  if (!ready_) {
    const uint32_t now = millis();
    if (lastTryMs_ != 0 && (now - lastTryMs_) < 3000) {
      return;
    }
    lastTryMs_ = now;
    if (!begin()) {
      return;
    }
  }
  if (shown_ && sameView(view, last_)) {
    return;
  }
  last_ = view;
  shown_ = true;
  draw(view);
}

}  // namespace bahn
