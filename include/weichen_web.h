#pragma once

#include "bahnhof.h"

namespace bahn {

// Loads a saved turnout table over the station defaults. Missing or damaged
// storage leaves the table unchanged.
void weichenLaden(Bahnhof& hof);

// Access point and page for editing the turnout table. Started with the station.
void weichenWebStarten(Bahnhof& hof);
void weichenWebLoop();

}  // namespace bahn
