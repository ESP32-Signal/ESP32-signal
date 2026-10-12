#pragma once

#include <stdint.h>

namespace bahn {

// Station desk, as distinct from a plain block module.
//
// A block module only watches one boundary: two block signals and one axle
// counter. A station adds the departure and entry signals. Three track keys
// choose the platform. Track key plus line key requests the line for departure.
// Entry-signal key plus track key clears the entry signal when the line is
// already set toward the station.
enum class Linienrichtung : uint8_t { Keine, Ausfahrt, Einfahrt };
enum class Begriff : uint8_t { Halt, Frei, Frei40 };

inline const char* begriffName(Begriff begriff) {
  switch (begriff) {
    case Begriff::Frei:
      return "Frei";
    case Begriff::Frei40:
      return "Frei 40";
    case Begriff::Halt:
      break;
  }
  return "Halt";
}

enum class BahnErgebnis : uint8_t {
  None,
  Festgelegt,
  AbgelehntGegenrichtung,
  AbgelehntZug,
  EinfahrtGestellt,
  AbgelehntKeineEinfahrt,
  AbgelehntGleis,
  Freigegeben,
};

struct BahnhofEvent {
  BahnErgebnis ergebnis = BahnErgebnis::None;
  bool festlegen = false;
  bool freigeben = false;
};

// Four turnouts. -1 leaves the turnout where it is, 0 and 1 are the two positions.
constexpr uint8_t kWeichenAnzahl = 4;
constexpr int8_t kWeicheBleibt = -1;

class Bahnhof {
 public:
  // gleis is 1..3. Locks the line for departure when the opposite direction is
  // not set and the line is empty. The exit signal stays at Halt until
  // signaleStellen(), which runs after the turnouts are thrown.
  BahnhofEvent ausfahrt(uint8_t gleis, bool streckeBelegt);
  // Selects the entry route when the line direction is Einfahrt. The entry
  // signal stays at Halt until signaleStellen().
  BahnhofEvent einfahrt(uint8_t gleis);
  // Turnout positions for the selected route. Unchanged turnouts are kWeicheBleibt.
  void weichen(int8_t lage[kWeichenAnzahl]) const;
  // Gives the selected signal its proceed aspect. Call this after the turnouts move.
  void signaleStellen();
  // Drops the exit signal while a train is on the line, and releases our own
  // departure lock once that train has left.
  BahnhofEvent poll(bool streckeBelegt);
  void onFestlegen();
  void onFreigeben();
  // towardLine: the axle entered the line. Otherwise it entered the station.
  void onAxle(bool towardLine);

  Linienrichtung richtung() const { return richtung_; }
  Begriff ausfahrtSignal() const { return ausfahrtSignal_; }
  Begriff einfahrtSignal() const { return einfahrtSignal_; }
  uint8_t gleis() const { return gleis_; }
  bool vorblockAusfahrt() const { return vorblock_; }
  // Track digits for the two platform displays. The unused direction is 0.
  void anzeige(uint8_t& ausfahrt, uint8_t& einfahrt) const;
  // Six routes: departure tracks 1..3, then entry tracks 1..3. Turnout values
  // are kWeicheBleibt, 0 or 1. Each route's proceed aspect is Frei or Frei40.
  // gleisAn switches the three platform tracks. A rejected edit leaves the
  // previous table in place.
  bool setTabelle(const int8_t ausfahrt[3][kWeichenAnzahl], const int8_t einfahrt[3][kWeichenAnzahl],
                  const Begriff ausfahrtSignal[3], const Begriff einfahrtSignal[3], const bool gleisAn[3]);
  void tabelle(int8_t ausfahrt[3][kWeichenAnzahl], int8_t einfahrt[3][kWeichenAnzahl], Begriff ausfahrtSignal[3],
               Begriff einfahrtSignal[3], bool gleisAn[3]) const;
  bool gleisAn(uint8_t gleis) const;

  Bahnhof();

 private:
  Linienrichtung richtung_ = Linienrichtung::Keine;
  Begriff ausfahrtSignal_ = Begriff::Halt;
  Begriff einfahrtSignal_ = Begriff::Halt;
  uint8_t gleis_ = 0;
  bool vorblock_ = false;
  bool fahrt_ = false;
  int8_t ausfahrtWeichen_[3][kWeichenAnzahl] = {};
  int8_t einfahrtWeichen_[3][kWeichenAnzahl] = {};
  Begriff ausfahrtBegriff_[3] = {};
  Begriff einfahrtBegriff_[3] = {};
  bool gleisAn_[3] = {true, true, true};
};

}  // namespace bahn
