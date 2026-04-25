#pragma once

#include "Model.h"
#include "Spacecraft.h"

#include <glm/glm.hpp>
#include <vector>

struct MoonDef {
    const char* name{"Moon"};
    double radius_km{0.0};
    double mass_kg{0.0};
    double rotation_period_s{0.0};
    int    diffuse_texture_index{-1};
    int    normal_texture_index{-1};
    float  roughness{0.9f};
    float  metallic{0.0f};
    float  emissive{0.0f};
    glm::vec3  albedo_color{0.45f, 0.45f, 0.45f};
    glm::dvec3 init_position_relative{};  // km, relative to parent planet
    glm::dvec3 init_velocity_relative{};  // km/s, relative to parent planet
};

struct MoonState {
    glm::dvec3 position_km{0.0};
    glm::dvec3 prev_position_km{0.0};
    glm::dvec3 velocity_km{0.0};
    glm::dvec3 prev_velocity_km{0.0};
    double rotation_angle{0.0};
    double prev_rotation_angle{0.0};
    int scene_object_index{-1};
    int parent_planet_index{-1};
    int moon_index{0};
};

struct PlanetDef {
    const char* name;
    double radius_km;
    double mass_kg;
    double semi_major_axis_km;
    double eccentricity;
    double orbital_period_s;
    double axial_tilt_rad;
    double rotation_period_s;
    bool   has_atmosphere;
    glm::vec3 atmosphere_color;
    float atmosphere_scale;
    int    diffuse_texture_index;
    int    normal_texture_index;
    int    cloud_texture_index;
    float  roughness;
    float  metallic;
    float  emissive;
    glm::vec3 albedo_color;
    glm::dvec3 init_position;
    glm::dvec3 init_velocity;
    std::vector<MoonDef> moons{};
};

struct PlanetState {
    double mean_anomaly{0.0};
    glm::dvec3 position_km{0.0};
    glm::dvec3 prev_position_km{0.0};   // position at start of current physics step
    glm::dvec3 velocity_km{0.0};
    glm::dvec3 prev_velocity_km{0.0};
    double rotation_angle{0.0};
    double prev_rotation_angle{0.0};    // rotation at start of current physics step
    int    scene_object_index{-1};
    int    atm_scene_object_index{-1};
};

struct SolarSystem {
    std::vector<PlanetDef>   defs;
    std::vector<PlanetState> states;
    std::vector<bool>        show_label;
    glm::dvec3 sun_position_km{0.0};
    int  sun_scene_object_index{-1};
    double simulation_time_s{3600.0}; // pre-seeded so first updateSolarSystem fires immediately
    double elapsed_simulation_s{0.0}; // total elapsed simulation time from epoch
    double render_alpha{0.0};        // interpolation fraction in [0, 1) for this frame
    double time_scale{1};
    bool paused{false};

    // Orbit ring rendering
    bool  show_orbits{true};
    float orbit_line_width{0.5f};
    float orbit_opacity{0.158f};
    bool  orbit_stippled{false};

    // Ecliptic grid
    bool  show_grid{true};
    float grid_line_width{0.5f};
    float grid_opacity{0.165f};
    int   grid_line_count{20};      // lines per side (total lines = 2*grid_line_count+1)
    float grid_spacing_km{5e8f};    // ~3.3 AU

    // Object selection (-1 = Sun/origin)
    int selected_body{-1};

    // Moons
    std::vector<MoonState> moon_states;
    std::vector<bool>      show_moon_label;
    int selected_moon{-1};

    // Spacecraft
    std::vector<SpacecraftDef>   spacecraft_defs;
    std::vector<SpacecraftState> spacecraft_states;
    std::vector<bool>            show_spacecraft_label;
    int   selected_spacecraft{-1};
    float spacecraft_rotation_rate{45.0f}; // degrees per second
    bool  spacecraft_follow_orbit{false};  // auto-align nose to orbital velocity

    // Spacecraft orbit ring (osculating Keplerian ellipse around Earth)
    bool  show_spacecraft_orbit{true};
    float spacecraft_orbit_line_width{1.0f};
    float spacecraft_orbit_opacity{0.6f};

    // Spacecraft predicted N-body path (forward integration)
    bool   show_spacecraft_path{false};
    double spacecraft_path_duration_s{8800.0}; // ~1.6 LEO orbits
    bool   spacecraft_path_dirty{true};

    // Maneuver planning mode
    bool   maneuver_mode{false};
    bool   maneuver_targets_initialized{false};
    int    maneuver_sc_idx{-1};
    double maneuver_t0_s{0.0};            // slider value [0, 3600]
    // Target absolute velocity at t0 in PRN frame (km/s). Default = current orbit.
    // Actual Δv = target − reference (computed each frame in updateSceneFromSolarSystem).
    double maneuver_prograde{0.0};
    double maneuver_radial{0.0};
    double maneuver_normal{0.0};
    // Reference velocity at t0 in PRN frame (updated each frame, read by GUI)
    double maneuver_ref_prograde{0.0};
    double maneuver_ref_radial{0.0};
    double maneuver_ref_normal{0.0};
};

SolarSystem createSolarSystem();
void updateSolarSystem(SolarSystem& ss, double delta_seconds);
Model createUVSphere(float radius, int stacks, int slices);

// Returns the render-interpolated world position for a body (index into ss.states).
// Always use this instead of state.position_km when positioning objects or the camera,
// so the camera target and rendered sphere move in lock-step.
[[nodiscard]] inline glm::dvec3 interpolatedPosition(SolarSystem const& ss, std::size_t idx)
{
    auto const& state = ss.states[idx];
    return glm::mix(state.prev_position_km, state.position_km, ss.render_alpha);
}

[[nodiscard]] inline glm::dvec3 interpolatedVelocity(SolarSystem const& ss, std::size_t idx)
{
    auto const& state = ss.states[idx];
    return glm::mix(state.prev_velocity_km, state.velocity_km, ss.render_alpha);
}

// Extrapolated position for a spacecraft: advances from the last physics step
// by the remaining unprocessed accumulator time using the current velocity.
// This keeps the craft visually smooth between the fixed 30s physics steps.
[[nodiscard]] inline glm::dvec3 interpolatedSpacecraftPosition(SolarSystem const& ss, std::size_t idx)
{
    auto const& sc = ss.spacecraft_states[idx];
    return sc.position_km + sc.velocity_km * sc.time_accumulator;
}

// Planet position at the same physics time as a given spacecraft's last step.
// Use this when pairing sc.position_km / sc.velocity_km with a planet position
// so that r_rel is self-consistent and doesn't drift between spacecraft steps.
[[nodiscard]] inline glm::dvec3 planetPositionAtSpacecraftTime(
    SolarSystem const& ss, std::size_t planet_idx, std::size_t sc_idx)
{
    auto const& sc    = ss.spacecraft_states[sc_idx];
    auto const& state = ss.states[planet_idx];
    double alpha = glm::clamp((ss.simulation_time_s - sc.time_accumulator) / 3600.0, 0.0, 1.0);
    return glm::mix(state.prev_position_km, state.position_km, alpha);
}

// Render-interpolated world position for a moon.
[[nodiscard]] inline glm::dvec3 interpolatedMoonPosition(SolarSystem const& ss, std::size_t moon_idx)
{
    auto const& s = ss.moon_states[moon_idx];
    return glm::mix(s.prev_position_km, s.position_km, ss.render_alpha);
}
