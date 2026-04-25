#pragma once

#include "Model.h"
#include "Maneuver.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

// Forward declaration
struct SolarSystem;

struct SpacecraftDef {
    const char* name{"Spacecraft-1"};
    double      mass_kg{10000.0};         // kg
    double      thrust_N{400000.0};       // max thrust in Newtons (200 kN → ~20 m/s² for 10t craft)
    double      visual_scale_km{0.005};   // uniform scale: 1 unit = 5m → craft is 5m × 30m × 5m
    glm::vec3   color{1.0f, 0.9f, 0.2f}; // golden yellow
};

struct SpacecraftState {
    glm::dvec3 position_km{0.0};
    glm::dvec3 prev_position_km{0.0};
    glm::dvec3 velocity_km{0.0};                    // km/s
    glm::dquat orientation{1.0, 0.0, 0.0, 0.0};    // world-space orientation; local +Y = nose
    double     thrust_level{0.0};                   // [0, 1] fraction of max thrust
    double     time_accumulator{0.0};               // simulation seconds pending integration
    int        scene_object_index{-1};

    // SOI tracking
    int  dominant_body_idx{3};   // index into ss.states (3 = Earth by default)
    bool dominant_is_moon{false};
    int  dominant_moon_idx{-1};  // index into ss.moon_states if dominant_is_moon

    // Approved maneuver queue
    std::vector<ManeuverNode> maneuvers;
};

// A flat box mesh: 2 wide (X) × 4 long (Y = forward/nose) × 0.5 thick (Z).
// Scaled at render time by SpacecraftDef::visual_scale_km.
Model createBoxMesh();

// Advance all spacecraft physics by scaled_dt simulation-seconds.
void updateSpacecrafts(std::vector<SpacecraftDef> const& defs,
                       std::vector<SpacecraftState>&      states,
                       SolarSystem const&                 ss,
                       double                             scaled_dt);

// Append a new spacecraft spawned in low Earth orbit (~400 km altitude).
void spawnSpacecraftAtEarth(std::vector<SpacecraftDef>&  defs,
                            std::vector<SpacecraftState>& states,
                            SolarSystem const&            ss);
