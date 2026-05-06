#include "Maneuver.h"
#include "SolarSystem.h"
#include "Spacecraft.h"
#include "PatchedConic.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

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

// Newton-Raphson for hyperbolic Kepler equation: M_H = e*sinh(F) - F
static double solveKeplerHyp(double M_H, double e)
{
    double F = (std::abs(M_H) < 1.0)
               ? M_H / (e - 1.0)
               : std::copysign(std::log(2.0 * std::abs(M_H) / e + 1.8), M_H);
    for (int i = 0; i < 50; ++i)
    {
        double const dF = (e * std::sinh(F) - F - M_H) / (e * std::cosh(F) - 1.0);
        F -= dF;
        if (std::abs(dF) < 1e-12) break;
    }
    return F;
}

std::pair<glm::dvec3, glm::dvec3>
keplerPropagate(glm::dvec3 r, glm::dvec3 v, double GM, double dt)
{
    double const r_mag  = glm::length(r);
    double const v_sq   = glm::dot(v, v);
    double const energy = v_sq / 2.0 - GM / r_mag;

    if (std::abs(energy) < 1e-30) return {r + v * dt, v}; // degenerate guard

    double const a = -GM / (2.0 * energy);

    glm::dvec3 const h      = glm::cross(r, v);
    double     const h_mag  = glm::length(h);
    glm::dvec3 const e_vec  = glm::cross(v, h) / GM - r / r_mag;
    double     const e      = glm::length(e_vec);

    glm::dvec3 const e_hat = (e > 1e-10) ? e_vec / e : glm::dvec3(1.0, 0.0, 0.0);
    glm::dvec3 const n_hat = (h_mag > 1e-10) ? h / h_mag : glm::dvec3(0.0, 1.0, 0.0);
    glm::dvec3 const q_hat = glm::cross(n_hat, e_hat);

    double const nu0 = std::atan2(glm::dot(q_hat, r), glm::dot(e_hat, r));

    if (a < 0.0) // hyperbolic
    {
        double const k    = std::sqrt((e - 1.0) / (e + 1.0));
        double const F0   = 2.0 * std::atanh(
            std::clamp(k * std::tan(nu0 / 2.0), -1.0 + 1e-12, 1.0 - 1e-12));
        double const M_H0 = e * std::sinh(F0) - F0;
        double const n_h  = std::sqrt(GM / ((-a) * (-a) * (-a)));
        double const F1   = solveKeplerHyp(M_H0 + n_h * dt, e);

        double const nu1    = 2.0 * std::atan(std::tanh(F1 / 2.0) / k);
        double const p      = a * (1.0 - e * e); // positive: |a|(e²-1)
        double const r1_mag = p / (1.0 + e * std::cos(nu1));

        glm::dvec3 const r1     = r1_mag * (std::cos(nu1) * e_hat + std::sin(nu1) * q_hat);
        double     const sqgmp  = std::sqrt(GM / p);
        double     const vr1    = sqgmp * e * std::sin(nu1);
        double     const vt1    = sqgmp * (1.0 + e * std::cos(nu1));
        glm::dvec3 const t1_hat = glm::normalize(glm::cross(n_hat, r1));
        glm::dvec3 const v1     = vr1 * glm::normalize(r1) + vt1 * t1_hat;

        return {r1, v1};
    }

    // Elliptic (a > 0)
    double const E0 = 2.0 * std::atan(std::sqrt((1.0 - e) / (1.0 + e)) * std::tan(nu0 / 2.0));
    double const n  = std::sqrt(GM / (a * a * a));
    double const M0 = E0 - e * std::sin(E0);
    double const E1 = solveKepler(M0 + n * dt, e);

    double const cos_E1 = std::cos(E1);
    double const sin_E1 = std::sin(E1);
    double const r1_mag = a * (1.0 - e * cos_E1);

    glm::dvec3 const r1 = (a * (cos_E1 - e)) * e_hat +
                          (a * std::sqrt(1.0 - e * e) * sin_E1) * q_hat;

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

ClosestApproachResult predictClosestApproach(
    SolarSystem ss, int sc_idx,
    int target_body_idx, int target_moon_idx,
    double scan_days)
{
    bool const is_moon = target_moon_idx >= 0;
    double const soi_km = is_moon
        ? ss.defs[target_body_idx].moons[target_moon_idx].soi_km
        : ss.defs[target_body_idx].soi_km;

    auto target_pos = [&](SolarSystem const& s) -> glm::dvec3 {
        if (is_moon) return s.moon_states[target_moon_idx].position_km;
        return s.states[target_body_idx].position_km;
    };

    double const scan_total  = scan_days * 86400.0;
    double const coarse_step = 3600.0;

    double min_dist       = std::numeric_limits<double>::max();
    double elapsed_origin = ss.elapsed_simulation_s;
    SolarSystem ss_chk    = ss;

    double elapsed_scan = 0.0;
    while (elapsed_scan < scan_total)
    {
        double const step = std::min(coarse_step, scan_total - elapsed_scan);
        updateSolarSystemPatchedConic(ss, step);
        updateSpacecraftSOI(ss, static_cast<std::size_t>(sc_idx));
        elapsed_scan += step;

        double const dist = glm::length(
            ss.spacecraft_states[sc_idx].position_km - target_pos(ss));

        if (dist < min_dist)
        {
            ss_chk   = ss;
            min_dist = dist;
        }
    }

    // Ternary-search refinement within ±2 coarse steps of the checkpoint
    double lo = 0.0;
    double hi = 2.0 * coarse_step;

    auto dist_at = [&](double offset) -> double {
        SolarSystem tmp = ss_chk;
        updateSolarSystemPatchedConic(tmp, offset);
        return glm::length(
            tmp.spacecraft_states[sc_idx].position_km - target_pos(tmp));
    };

    for (int i = 0; i < 60 && (hi - lo) > 1.0; ++i)
    {
        double const m1 = lo + (hi - lo) / 3.0;
        double const m2 = hi - (hi - lo) / 3.0;
        if (dist_at(m1) < dist_at(m2)) hi = m2;
        else                            lo = m1;
    }

    updateSolarSystemPatchedConic(ss_chk, (lo + hi) / 2.0);
    double const refined_dist = glm::length(
        ss_chk.spacecraft_states[sc_idx].position_km - target_pos(ss_chk));

    ClosestApproachResult result;
    result.min_distance_km   = refined_dist;
    result.elapsed_s_at_min  = ss_chk.elapsed_simulation_s;
    result.days_from_now     = (ss_chk.elapsed_simulation_s - elapsed_origin) / 86400.0;
    result.soi_entered       = refined_dist < soi_km;
    result.soi_radius_km     = soi_km;
    return result;
}
