#pragma once

#include <stdint.h>

namespace bahn {

enum class PairView : uint8_t { Nein, Still, Aktiv };
enum class SignalView : uint8_t { Halt, Frei, Frei40 };
enum class RichtungView : uint8_t { Keine, NachA, NachB, Ausfahrt, Einfahrt };

// Snapshot drawn on the 128x64 status display.
// On a block module signalA/signalB are the two block signals.
// On a station signalA is the exit signal and signalB the entry signal.
struct ModuleView {
  bool bahnhof = false;
  PairView pairA = PairView::Nein;
  PairView pairB = PairView::Nein;
  SignalView signalA = SignalView::Halt;
  SignalView signalB = SignalView::Halt;
  RichtungView richtung = RichtungView::Keine;
  int16_t axleNet = 0;
};

PairView pairView(bool paired, bool online);

// SSD1306 status screen. show() redraws only when the snapshot changes and
// retries the bus if the display was missing at startup.
class Oled {
 public:
  bool begin();
  void show(const ModuleView& view);

 private:
  bool ready_ = false;
  bool shown_ = false;
  uint32_t lastTryMs_ = 0;
  ModuleView last_{};
};

}  // namespace bahn
