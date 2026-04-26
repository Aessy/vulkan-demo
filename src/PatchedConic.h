#pragma once

#include "SolarSystem.h"

// Advance the full solar system (planets, moons, spacecraft) by delta_seconds
// using patched-conic Kepler propagation. Drop-in replacement for updateSolarSystem().
void updateSolarSystemPatchedConic(SolarSystem& ss, double delta_seconds);
