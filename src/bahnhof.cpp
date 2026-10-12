#include "bahnhof.h"

#include <string.h>

namespace bahn {
namespace {

bool gleisOk(uint8_t gleis) { return gleis >= 1 && gleis <= 3; }

// Stellung 0 is straight, Stellung 1 is diverging.
// Departure uses turnouts 1 and 2 toward the line. Entry uses 3 and 4
// at the other end of the tracks. The unused throat stays as it is.
constexpr int8_t kAusfahrtWeichen[3][kWeichenAnzahl] = {
    {0, kWeicheBleibt, kWeicheBleibt, kWeicheBleibt},
    {1, 0, kWeicheBleibt, kWeicheBleibt},
    {1, 1, kWeicheBleibt, kWeicheBleibt},
};
constexpr int8_t kEinfahrtWeichen[3][kWeichenAnzahl] = {
    {kWeicheBleibt, kWeicheBleibt, 0, kWeicheBleibt},
    {kWeicheBleibt, kWeicheBleibt, 1, 0},
    {kWeicheBleibt, kWeicheBleibt, 1, 1},
};
// Straight platform is Frei. A diverging route is Frei 40, which also pulls the yellow relay.
constexpr Begriff kAusfahrtBegriff[3] = {Begriff::Frei, Begriff::Frei40, Begriff::Frei40};
constexpr Begriff kEinfahrtBegriff[3] = {Begriff::Frei, Begriff::Frei40, Begriff::Frei40};
constexpr bool kGleisAn[3] = {true, true, true};

bool lageOk(int8_t lage) { return lage == kWeicheBleibt || lage == 0 || lage == 1; }

bool begriffOk(Begriff begriff) { return begriff == Begriff::Frei || begriff == Begriff::Frei40; }

bool tabelleOk(const int8_t rows[3][kWeichenAnzahl]) {
  for (uint8_t gleis = 0; gleis < 3; ++gleis) {
    for (uint8_t weiche = 0; weiche < kWeichenAnzahl; ++weiche) {
      if (!lageOk(rows[gleis][weiche])) {
        return false;
      }
    }
  }
  return true;
}

bool begriffeOk(const Begriff row[3]) {
  for (uint8_t gleis = 0; gleis < 3; ++gleis) {
    if (!begriffOk(row[gleis])) {
      return false;
    }
  }
  return true;
}

}  // namespace

Bahnhof::Bahnhof() { setTabelle(kAusfahrtWeichen, kEinfahrtWeichen, kAusfahrtBegriff, kEinfahrtBegriff, kGleisAn); }

BahnhofEvent Bahnhof::ausfahrt(uint8_t gleis, bool streckeBelegt) {
  BahnhofEvent event;
  if (!gleisOk(gleis)) {
    return event;
  }
  if (!gleisAn_[gleis - 1]) {
    event.ergebnis = BahnErgebnis::AbgelehntGleis;
    return event;
  }
  if (richtung_ == Linienrichtung::Einfahrt) {
    event.ergebnis = BahnErgebnis::AbgelehntGegenrichtung;
    return event;
  }
  if (streckeBelegt) {
    event.ergebnis = BahnErgebnis::AbgelehntZug;
    return event;
  }

  const bool neu = richtung_ != Linienrichtung::Ausfahrt;
  richtung_ = Linienrichtung::Ausfahrt;
  gleis_ = gleis;
  event.festlegen = neu;
  event.ergebnis = BahnErgebnis::Festgelegt;
  return event;
}

BahnhofEvent Bahnhof::einfahrt(uint8_t gleis) {
  BahnhofEvent event;
  if (!gleisOk(gleis)) {
    return event;
  }
  if (!gleisAn_[gleis - 1]) {
    event.ergebnis = BahnErgebnis::AbgelehntGleis;
    return event;
  }
  if (richtung_ != Linienrichtung::Einfahrt) {
    event.ergebnis = BahnErgebnis::AbgelehntKeineEinfahrt;
    return event;
  }
  gleis_ = gleis;
  event.ergebnis = BahnErgebnis::EinfahrtGestellt;
  return event;
}

BahnhofEvent Bahnhof::poll(bool streckeBelegt) {
  BahnhofEvent event;
  if (richtung_ != Linienrichtung::Ausfahrt) {
    return event;
  }
  if (streckeBelegt) {
    fahrt_ = true;
    ausfahrtSignal_ = Begriff::Halt;
    vorblock_ = false;
    return event;
  }
  if (!fahrt_) {
    return event;
  }
  richtung_ = Linienrichtung::Keine;
  fahrt_ = false;
  gleis_ = 0;
  ausfahrtSignal_ = Begriff::Halt;
  vorblock_ = false;
  event.freigeben = true;
  event.ergebnis = BahnErgebnis::Freigegeben;
  return event;
}

void Bahnhof::onFestlegen() {
  if (richtung_ == Linienrichtung::Ausfahrt) {
    return;
  }
  richtung_ = Linienrichtung::Einfahrt;
}

void Bahnhof::onFreigeben() {
  if (richtung_ != Linienrichtung::Einfahrt) {
    return;
  }
  richtung_ = Linienrichtung::Keine;
  einfahrtSignal_ = Begriff::Halt;
  gleis_ = 0;
}

bool Bahnhof::setTabelle(const int8_t ausfahrt[3][kWeichenAnzahl], const int8_t einfahrt[3][kWeichenAnzahl],
                         const Begriff ausfahrtSignal[3], const Begriff einfahrtSignal[3], const bool gleisAn[3]) {
  if (!tabelleOk(ausfahrt) || !tabelleOk(einfahrt) || !begriffeOk(ausfahrtSignal) || !begriffeOk(einfahrtSignal)) {
    return false;
  }
  memcpy(ausfahrtWeichen_, ausfahrt, sizeof ausfahrtWeichen_);
  memcpy(einfahrtWeichen_, einfahrt, sizeof einfahrtWeichen_);
  memcpy(ausfahrtBegriff_, ausfahrtSignal, sizeof ausfahrtBegriff_);
  memcpy(einfahrtBegriff_, einfahrtSignal, sizeof einfahrtBegriff_);
  memcpy(gleisAn_, gleisAn, sizeof gleisAn_);
  return true;
}

void Bahnhof::tabelle(int8_t ausfahrt[3][kWeichenAnzahl], int8_t einfahrt[3][kWeichenAnzahl], Begriff ausfahrtSignal[3],
                      Begriff einfahrtSignal[3], bool gleisAn[3]) const {
  memcpy(ausfahrt, ausfahrtWeichen_, sizeof ausfahrtWeichen_);
  memcpy(einfahrt, einfahrtWeichen_, sizeof einfahrtWeichen_);
  memcpy(ausfahrtSignal, ausfahrtBegriff_, sizeof ausfahrtBegriff_);
  memcpy(einfahrtSignal, einfahrtBegriff_, sizeof einfahrtBegriff_);
  memcpy(gleisAn, gleisAn_, sizeof gleisAn_);
}

bool Bahnhof::gleisAn(uint8_t gleis) const { return gleisOk(gleis) && gleisAn_[gleis - 1]; }

void Bahnhof::weichen(int8_t lage[kWeichenAnzahl]) const {
  const int8_t* row = nullptr;
  if (gleisOk(gleis_) && richtung_ == Linienrichtung::Ausfahrt) {
    row = ausfahrtWeichen_[gleis_ - 1];
  } else if (gleisOk(gleis_) && richtung_ == Linienrichtung::Einfahrt) {
    row = einfahrtWeichen_[gleis_ - 1];
  }
  for (uint8_t i = 0; i < kWeichenAnzahl; ++i) {
    lage[i] = row == nullptr ? kWeicheBleibt : row[i];
  }
}

void Bahnhof::signaleStellen() {
  if (!gleisOk(gleis_) || fahrt_) {
    return;
  }
  if (richtung_ == Linienrichtung::Ausfahrt) {
    vorblock_ = true;
    ausfahrtSignal_ = ausfahrtBegriff_[gleis_ - 1];
  } else if (richtung_ == Linienrichtung::Einfahrt) {
    einfahrtSignal_ = einfahrtBegriff_[gleis_ - 1];
  }
}

void Bahnhof::anzeige(uint8_t& ausfahrt, uint8_t& einfahrt) const {
  ausfahrt = 0;
  einfahrt = 0;
  if (!gleisOk(gleis_)) {
    return;
  }
  if (richtung_ == Linienrichtung::Ausfahrt) {
    ausfahrt = gleis_;
  } else if (richtung_ == Linienrichtung::Einfahrt) {
    einfahrt = gleis_;
  }
}

void Bahnhof::onAxle(bool towardLine) {
  if (towardLine) {
    if (richtung_ == Linienrichtung::Ausfahrt) {
      fahrt_ = true;
      ausfahrtSignal_ = Begriff::Halt;
      vorblock_ = false;
    }
    return;
  }
  einfahrtSignal_ = Begriff::Halt;
}

}  // namespace bahn
