#include "bahnhof.h"

#include <stdio.h>

namespace {

int failures = 0;

void expect(bool cond, const char* name) {
  if (cond) {
    return;
  }
  fprintf(stderr, "FAIL %s\n", name);
  failures++;
}

void expectAnzeige(const bahn::Bahnhof& hof, uint8_t ausfahrt, uint8_t einfahrt, const char* name) {
  uint8_t aus = 9;
  uint8_t ein = 9;
  hof.anzeige(aus, ein);
  expect(aus == ausfahrt && ein == einfahrt, name);
}

void expectWeichen(const bahn::Bahnhof& hof, int8_t w1, int8_t w2, int8_t w3, int8_t w4, const char* name) {
  int8_t lage[bahn::kWeichenAnzahl] = {};
  hof.weichen(lage);
  expect(lage[0] == w1 && lage[1] == w2 && lage[2] == w3 && lage[3] == w4, name);
}

void testAusfahrt() {
  bahn::Bahnhof hof;
  const bahn::BahnhofEvent locked = hof.ausfahrt(2, false);
  expect(locked.ergebnis == bahn::BahnErgebnis::Festgelegt && locked.festlegen, "departure locks the line");
  expect(hof.richtung() == bahn::Linienrichtung::Ausfahrt, "direction is departure");
  expect(hof.ausfahrtSignal() == bahn::Begriff::Halt, "exit signal waits until the turnouts are set");
  expectWeichen(hof, 1, 0, bahn::kWeicheBleibt, bahn::kWeicheBleibt, "departure track 2 sets the exit throat");
  hof.signaleStellen();
  expect(hof.ausfahrtSignal() == bahn::Begriff::Frei40 && hof.vorblockAusfahrt(), "diverging exit shows Frei 40");
  expect(hof.gleis() == 2, "track 2 selected");
  expectAnzeige(hof, 2, 0, "departure display shows track 2");

  const bahn::BahnhofEvent again = hof.ausfahrt(1, false);
  expect(again.ergebnis == bahn::BahnErgebnis::Festgelegt && !again.festlegen, "same direction keeps the lock");
  expectWeichen(hof, 0, bahn::kWeicheBleibt, bahn::kWeicheBleibt, bahn::kWeicheBleibt, "departure track 1");
  hof.signaleStellen();
  expect(hof.gleis() == 1 && hof.ausfahrtSignal() == bahn::Begriff::Frei, "track change refreshes the exit signal");

  expect(hof.poll(false).freigeben == false, "empty line before a train does not release");
  expect(hof.poll(true).ergebnis == bahn::BahnErgebnis::None, "train on the line holds the direction");
  expect(hof.ausfahrtSignal() == bahn::Begriff::Halt && hof.richtung() == bahn::Linienrichtung::Ausfahrt,
         "exit signal drops as the train enters");
  expectAnzeige(hof, 1, 0, "departure digit stays while the train is on the line");
  const bahn::BahnhofEvent done = hof.poll(false);
  expect(done.freigeben && done.ergebnis == bahn::BahnErgebnis::Freigegeben, "line releases after the train has left");
  expect(hof.richtung() == bahn::Linienrichtung::Keine, "direction cleared");
  expectAnzeige(hof, 0, 0, "displays go dark when the line is released");
}

void testRefuse() {
  bahn::Bahnhof occupied;
  const bahn::BahnhofEvent train = occupied.ausfahrt(1, true);
  expect(train.ergebnis == bahn::BahnErgebnis::AbgelehntZug && !train.festlegen, "train on the line blocks the lock");
  expect(occupied.richtung() == bahn::Linienrichtung::Keine, "refused lock changes nothing");

  bahn::Bahnhof inbound;
  inbound.onFestlegen();
  const bahn::BahnhofEvent against = inbound.ausfahrt(3, false);
  expect(against.ergebnis == bahn::BahnErgebnis::AbgelehntGegenrichtung, "opposite direction blocks the lock");
  expect(inbound.richtung() == bahn::Linienrichtung::Einfahrt, "inbound direction stays");

  const bahn::BahnhofEvent closed = inbound.einfahrt(0);
  expect(closed.ergebnis == bahn::BahnErgebnis::None, "track 0 is not a platform");
}

void testEinfahrt() {
  bahn::Bahnhof hof;
  const bahn::BahnhofEvent early = hof.einfahrt(1);
  expect(early.ergebnis == bahn::BahnErgebnis::AbgelehntKeineEinfahrt, "entry needs the inbound direction");
  expect(hof.einfahrtSignal() == bahn::Begriff::Halt, "entry signal stays at stop");

  hof.onFestlegen();
  expectAnzeige(hof, 0, 0, "entry direction without a track stays dark");
  const bahn::BahnhofEvent open = hof.einfahrt(3);
  expect(open.ergebnis == bahn::BahnErgebnis::EinfahrtGestellt, "entry route is selected");
  expect(hof.einfahrtSignal() == bahn::Begriff::Halt, "entry signal waits until the turnouts are set");
  expectWeichen(hof, bahn::kWeicheBleibt, bahn::kWeicheBleibt, 1, 1, "entry track 3 sets the entry throat");
  hof.signaleStellen();
  expect(hof.einfahrtSignal() == bahn::Begriff::Frei40 && hof.gleis() == 3, "diverging entry shows Frei 40");
  expectAnzeige(hof, 0, 3, "entry display shows track 3");

  hof.onAxle(false);
  expect(hof.einfahrtSignal() == bahn::Begriff::Halt, "entry signal drops as the train arrives");
  expect(hof.richtung() == bahn::Linienrichtung::Einfahrt, "arrival does not release the line");

  hof.onFreigeben();
  expect(hof.richtung() == bahn::Linienrichtung::Keine && hof.gleis() == 0, "peer release clears the inbound lock");
  expectAnzeige(hof, 0, 0, "entry display goes dark on release");
}

void testTabelle() {
  bahn::Bahnhof hof;
  int8_t ausfahrt[3][bahn::kWeichenAnzahl];
  int8_t einfahrt[3][bahn::kWeichenAnzahl];
  bahn::Begriff ausfahrtSignal[3];
  bahn::Begriff einfahrtSignal[3];
  bool gleisAn[3] = {true, true, true};
  hof.tabelle(ausfahrt, einfahrt, ausfahrtSignal, einfahrtSignal, gleisAn);
  expect(ausfahrtSignal[0] == bahn::Begriff::Frei && ausfahrtSignal[1] == bahn::Begriff::Frei40, "straight exit is Frei");
  expect(gleisAn[0] && gleisAn[1] && gleisAn[2], "all tracks start switched on");
  ausfahrt[0][1] = 1;
  ausfahrtSignal[0] = bahn::Begriff::Frei40;
  expect(hof.setTabelle(ausfahrt, einfahrt, ausfahrtSignal, einfahrtSignal, gleisAn), "a valid table is stored");
  hof.ausfahrt(1, false);
  expectWeichen(hof, 0, 1, bahn::kWeicheBleibt, bahn::kWeicheBleibt, "edited departure track 1");
  hof.signaleStellen();
  expect(hof.ausfahrtSignal() == bahn::Begriff::Frei40, "edited exit aspect is Frei 40");

  ausfahrt[0][0] = 2;
  gleisAn[2] = false;
  expect(!hof.setTabelle(ausfahrt, einfahrt, ausfahrtSignal, einfahrtSignal, gleisAn), "an unknown position is rejected");
  expectWeichen(hof, 0, 1, bahn::kWeicheBleibt, bahn::kWeicheBleibt, "rejected edit keeps the previous table");
  expect(hof.ausfahrtSignal() == bahn::Begriff::Frei40, "rejected edit keeps the signal aspect");
  expect(hof.gleisAn(3), "rejected edit keeps the track switch");

  ausfahrt[0][0] = 0;
  ausfahrtSignal[0] = bahn::Begriff::Halt;
  gleisAn[2] = true;
  expect(!hof.setTabelle(ausfahrt, einfahrt, ausfahrtSignal, einfahrtSignal, gleisAn), "Halt is not a route aspect");
  hof.signaleStellen();
  expect(hof.ausfahrtSignal() == bahn::Begriff::Frei40, "rejected aspect keeps Frei 40");

  gleisAn[1] = false;
  expect(hof.setTabelle(ausfahrt, einfahrt, ausfahrtSignal, einfahrtSignal, gleisAn) == false, "Halt is still rejected");
  ausfahrtSignal[0] = bahn::Begriff::Frei40;
  expect(hof.setTabelle(ausfahrt, einfahrt, ausfahrtSignal, einfahrtSignal, gleisAn), "a track can be switched off");
  const bahn::BahnhofEvent blocked = hof.ausfahrt(2, false);
  expect(blocked.ergebnis == bahn::BahnErgebnis::AbgelehntGleis && !blocked.festlegen, "a switched-off track refuses the route");
  expect(hof.gleis() == 1, "the previous route stays");
  hof.onFestlegen();
  expect(hof.einfahrt(2).ergebnis == bahn::BahnErgebnis::AbgelehntGleis, "entry also refuses a switched-off track");
}

void testWeichen() {
  bahn::Bahnhof aus;
  aus.ausfahrt(3, false);
  expectWeichen(aus, 1, 1, bahn::kWeicheBleibt, bahn::kWeicheBleibt, "departure track 3");

  bahn::Bahnhof ein;
  ein.onFestlegen();
  ein.einfahrt(1);
  expectWeichen(ein, bahn::kWeicheBleibt, bahn::kWeicheBleibt, 0, bahn::kWeicheBleibt, "entry track 1");
  ein.einfahrt(2);
  expectWeichen(ein, bahn::kWeicheBleibt, bahn::kWeicheBleibt, 1, 0, "entry track 2");

  bahn::Bahnhof idle;
  expectWeichen(idle, bahn::kWeicheBleibt, bahn::kWeicheBleibt, bahn::kWeicheBleibt, bahn::kWeicheBleibt,
                "no route leaves every turnout");
}

void testOwnLockIgnoresPeer() {
  bahn::Bahnhof hof;
  hof.ausfahrt(1, false);
  hof.signaleStellen();
  hof.onFestlegen();
  hof.onFreigeben();
  expect(hof.richtung() == bahn::Linienrichtung::Ausfahrt, "our departure lock ignores the peer");
  hof.onAxle(true);
  expect(hof.ausfahrtSignal() == bahn::Begriff::Halt && hof.vorblockAusfahrt() == false, "first axle consumes the Vorblock");
}

}  // namespace

int main() {
  testAusfahrt();
  testRefuse();
  testEinfahrt();
  testWeichen();
  testTabelle();
  testOwnLockIgnoresPeer();
  if (failures != 0) {
    fprintf(stderr, "%d failed\n", failures);
    return 1;
  }
  printf("ok\n");
  return 0;
}
