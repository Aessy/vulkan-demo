#pragma once

#include <glm/glm.hpp>
#include <utility>

struct SolarSystem;

struct ManeuverNode {
    double t0_s{0.0};            // sim-seconds from approval until burn start
    double t0_abs_s{0.0};        // elapsed_simulation_s at approval + t0_s
    double prograde_dv{0.0};     // km/s (+ = forward along velocity)
    double radial_dv{0.0};       // km/s (+ = away from dominant body)
    double normal_dv{0.0};       // km/s (+ = orbit normal)
    bool   approved{false};
    bool   completed{false};
    bool   lock_attitude{false};
    double accumulated_dv{0.0};  // km/s applied so far

    // Precomputed each frame while planning/active
    glm::dvec3 burn_pos_rel{};   // pos at t0 relative to dominant body
    glm::dvec3 burn_vel_rel{};   // vel at t0 relative to dominant body
    glm::dvec3 delta_v_world{};  // world-space Δv vector

    // Dominant body at approval time — needed to re-anchor arc rendering
    // after the spacecraft has escaped to a different SOI.
    int  burn_dominant_body_idx{0};
    bool burn_dominant_is_moon{false};
    int  burn_dominant_moon_idx{-1};
};

// Solve Kepler's equation M = E - e*sin(E) via Newton-Raphson.
double solveKepler(double M, double e);

// Advance state (r, v) relative to a body with given GM by dt seconds.
std::pair<glm::dvec3, glm::dvec3>
keplerPropagate(glm::dvec3 r, glm::dvec3 v, double GM, double dt);

// Convert prograde/radial/normal scalars → world-space Δv vector.
// r and v are position and velocity relative to the dominant body.
glm::dvec3 dvWorld(glm::dvec3 r, glm::dvec3 v,
                   double prograde, double radial, double normal);

// Update sc.dominant_body_idx / dominant_is_moon / dominant_moon_idx
// for the spacecraft at sc_idx.  Call once per frame.
void updateSpacecraftSOI(SolarSystem& ss, std::size_t sc_idx);
