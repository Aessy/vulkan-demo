#include "Maneuver.h"
#include "SolarSystem.h"
#include "Spacecraft.h"

#include <glm/glm.hpp>
#include <cmath>

static constexpr double G_km3 = 6.674e-20; // km^3 / (kg * s^2)

// ---------------------------------------------------------------------------
// Kepler propagation
// ---------------------------------------------------------------------------

double solveKepler(double M, double e)
{
    double E = M;
    for (int i = 0; i < 50; ++i)
    {
        double dE = (M - E + e * std::sin(E)) / (1.0 - e * std::cos(E));
        E += dE;
        if (std::abs(dE) < 1e-12) break;
    }
    return E;
}

std::pair<glm::dvec3, glm::dvec3>
keplerPropagate(glm::dvec3 r, glm::dvec3 v, double GM, double dt)
{
    double const r_mag = glm::length(r);
    double const v_sq  = glm::dot(v, v);
    double const energy = v_sq / 2.0 - GM / r_mag;

    if (energy >= 0.0)
    {
        // Hyperbolic/parabolic — just linear extrapolation as fallback
        return {r + v * dt, v};
    }

    double const a = -GM / (2.0 * energy);

    glm::dvec3 const h      = glm::cross(r, v);
    double     const h_mag  = glm::length(h);
    glm::dvec3 const e_vec  = glm::cross(v, h) / GM - r / r_mag;
    double     const e      = glm::length(e_vec);

    glm::dvec3 const e_hat = (e > 1e-10) ? e_vec / e : glm::dvec3(1.0, 0.0, 0.0);
    glm::dvec3 const n_hat = (h_mag > 1e-10) ? h / h_mag : glm::dvec3(0.0, 1.0, 0.0);
    glm::dvec3 const q_hat = glm::cross(n_hat, e_hat);

    // True anomaly at current position, then convert to eccentric anomaly
    double const nu0     = std::atan2(glm::dot(q_hat, r), glm::dot(e_hat, r));
    double const E0_correct = 2.0 * std::atan(std::sqrt((1.0 - e) / (1.0 + e)) * std::tan(nu0 / 2.0));

    double const n  = std::sqrt(GM / (a * a * a)); // mean motion
    double const M0 = E0_correct - e * std::sin(E0_correct);
    double const M1 = M0 + n * dt;
    double const E1 = solveKepler(M1, e);

    // Position and velocity from E1
    double const cos_E1 = std::cos(E1);
    double const sin_E1 = std::sin(E1);
    double const r1_mag = a * (1.0 - e * cos_E1);

    double const x1 = a * (cos_E1 - e);
    double const y1 = a * std::sqrt(1.0 - e * e) * sin_E1;

    glm::dvec3 const r1 = x1 * e_hat + y1 * q_hat;

    double const vx1 = -a * a * n * sin_E1                          / r1_mag;
    double const vy1 =  a * a * n * std::sqrt(1.0 - e * e) * cos_E1 / r1_mag;
    glm::dvec3 const v1 = vx1 * e_hat + vy1 * q_hat;

    return {r1, v1};
}

// ---------------------------------------------------------------------------
// PRN frame Δv
// ---------------------------------------------------------------------------

glm::dvec3 dvWorld(glm::dvec3 r, glm::dvec3 v,
                   double prograde, double radial, double normal)
{
    glm::dvec3 const prograde_hat = glm::length(v) > 1e-15 ? glm::normalize(v) : glm::dvec3(0,1,0);
    glm::dvec3 const radial_hat   = glm::length(r) > 1e-15 ? glm::normalize(r) : glm::dvec3(1,0,0);
    glm::dvec3 const normal_hat   = glm::normalize(glm::cross(r, v));

    return prograde * prograde_hat + radial * radial_hat + normal * normal_hat;
}

// ---------------------------------------------------------------------------
// SOI tracking
// ---------------------------------------------------------------------------

void updateSpacecraftSOI(SolarSystem& ss, std::size_t sc_idx)
{
    if (sc_idx >= ss.spacecraft_states.size()) return;
    auto& sc = ss.spacecraft_states[sc_idx];

    // Check moons first (smallest SOIs take priority)
    for (std::size_t k = 0; k < ss.moon_states.size(); ++k)
    {
        auto const& ms       = ss.moon_states[k];
        auto const& par_def  = ss.defs[static_cast<std::size_t>(ms.parent_planet_index)];
        auto const& moon_def = par_def.moons[static_cast<std::size_t>(ms.moon_index)];
        if (moon_def.soi_km <= 0.0) continue;
        glm::dvec3 const moon_pos = glm::mix(ms.prev_position_km, ms.position_km, ss.render_alpha);
        if (glm::length(sc.position_km - moon_pos) < moon_def.soi_km)
        {
            sc.dominant_body_idx = ms.parent_planet_index;
            sc.dominant_is_moon  = true;
            sc.dominant_moon_idx = static_cast<int>(k);
            return;
        }
    }

    // Check planets (skip Sun at index 0)
    for (std::size_t i = 1; i < ss.defs.size(); ++i)
    {
        auto const& def = ss.defs[i];
        if (def.soi_km <= 0.0) continue;
        glm::dvec3 const planet_pos = glm::mix(ss.states[i].prev_position_km,
                                                ss.states[i].position_km, ss.render_alpha);
        if (glm::length(sc.position_km - planet_pos) < def.soi_km)
        {
            sc.dominant_body_idx = static_cast<int>(i);
            sc.dominant_is_moon  = false;
            sc.dominant_moon_idx = -1;
            return;
        }
    }

    // Default: Sun
    sc.dominant_body_idx = 0;
    sc.dominant_is_moon  = false;
    sc.dominant_moon_idx = -1;
}
