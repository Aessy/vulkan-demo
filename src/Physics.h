#pragma once

#include <glm/glm.hpp>
#include <span>
#include <cmath>

struct OsculatingOrbit {
    double     a;      // semi-major axis (km); negative for hyperbolic
    double     e;      // eccentricity
    glm::dvec3 e_hat;  // unit vector toward periapsis
    glm::dvec3 q_hat;  // 90° ahead in orbital plane
    glm::dvec3 h_hat;  // orbit normal (angular momentum direction)

    // Periapsis distance from body centre (valid for both elliptic and hyperbolic).
    [[nodiscard]] double periapsis_km() const { return a * (1.0 - e); }
    // Apoapsis distance — only meaningful when e < 1.
    [[nodiscard]] double apoapsis_km()  const { return a * (1.0 + e); }
};

inline OsculatingOrbit computeOsculatingOrbit(glm::dvec3 r, glm::dvec3 v, double GM)
{
    double const r_mag  = glm::length(r);
    double const v_sq   = glm::dot(v, v);
    double const energy = v_sq / 2.0 - GM / r_mag;
    double const a      = -GM / (2.0 * energy);

    glm::dvec3 const h     = glm::cross(r, v);
    double     const h_mag = glm::length(h);
    glm::dvec3 const e_vec = glm::cross(v, h) / GM - r / r_mag;
    double     const e     = glm::length(e_vec);

    glm::dvec3 const e_hat = (e > 1e-10) ? e_vec / e : glm::dvec3(1.0, 0.0, 0.0);
    glm::dvec3 const h_hat = (h_mag > 1e-10) ? h / h_mag : glm::dvec3(0.0, 1.0, 0.0);
    glm::dvec3 const q_hat = glm::cross(h_hat, e_hat);

    return { a, e, e_hat, q_hat, h_hat };
}

struct Attractor {
    glm::dvec3 pos_begin;
    glm::dvec3 pos_end;
    double      GM;
};

// Leapfrog KDK (Kick-Drift-Kick) symplectic integrator.
// Conserves a modified energy exactly, keeping orbits stable over arbitrary
// simulation lengths, provided dt is fixed.
// Each attractor supplies positions at the start and end of the step so that
// moving attractors are handled correctly.
// extra_accel is added to both kicks (e.g. constant thrust).
inline void leapfrogKDK(glm::dvec3& position, glm::dvec3& velocity,
                         std::span<Attractor const> attractors, double dt,
                         glm::dvec3 extra_accel = glm::dvec3(0.0))
{
    auto gravAccel = [](glm::dvec3 const& pos, glm::dvec3 const& attractor, double GM) {
        glm::dvec3 r  = pos - attractor;
        double     rm = glm::length(r);
        return -(GM / (rm * rm * rm)) * r;
    };

    glm::dvec3 acc0 = extra_accel;
    for (auto const& a : attractors)
        acc0 += gravAccel(position, a.pos_begin, a.GM);
    glm::dvec3 vel_half = velocity + acc0 * (dt * 0.5);
    position           += vel_half * dt;

    glm::dvec3 acc1 = extra_accel;
    for (auto const& a : attractors)
        acc1 += gravAccel(position, a.pos_end, a.GM);
    velocity = vel_half + acc1 * (dt * 0.5);
}
