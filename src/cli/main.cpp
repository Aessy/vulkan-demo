#include "SolarSystem.h"
#include "Spacecraft.h"
#include "Maneuver.h"
#include "PatchedConic.h"
#include "Physics.h"

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <print>
#include <ranges>
#include <sstream>
#include <string>
// ── Epoch ─────────────────────────────────────────────────────────────────────

// Returns simulation seconds elapsed since J2000.0 (2000-01-01 12:00 UTC)
// for a given calendar date (interpreted as noon UTC).
static double dateToElapsed(int year, int month, int day)
{
    int const a   = (14 - month) / 12;
    int const y   = year + 4800 - a;
    int const m   = month + 12 * a - 3;
    // All divisions here are intentional integer floor-division (standard JDN formula).
    int const jdn = day + (153*m + 2)/5 + 365*y + y/4 - y/100 + y/400 - 32045;
    return (static_cast<double>(jdn) - 2451545.0) * 86400.0;
}

// ── Context ───────────────────────────────────────────────────────────────────

struct PendingManeuver {
    double prograde_dv{0.0};
    double radial_dv{0.0};
    double normal_dv{0.0};
    double t0_seconds{0.0};
    bool   valid{false};
};

struct CliContext {
    SolarSystem     ss;
    int             selected_sc{-1};
    PendingManeuver pending{};
};

// ── Lambert solver ────────────────────────────────────────────────────────────

static void stumpffCS(double z, double& C, double& S)
{
    if (z > 1e-6) {
        double const sqz = std::sqrt(z);
        C = (1.0 - std::cos(sqz)) / z;
        S = (sqz - std::sin(sqz)) / (z * sqz);
    } else if (z < -1e-6) {
        double const sqz = std::sqrt(-z);
        C = (std::cosh(sqz) - 1.0) / (-z);
        S = (std::sinh(sqz) - sqz) / ((-z) * sqz);
    } else {
        C = 0.5;
        S = 1.0 / 6.0;
    }
}

struct LambertSolution {
    glm::dvec3 v1{};
    glm::dvec3 v2{};
    bool       converged{false};
};

// Solve Lambert's problem: find heliocentric velocities v1, v2 connecting r1→r2 in tof_s.
// use_positive_A=true gives the prograde short-way (or long-way retrograde) branch;
// use_positive_A=false gives the other branch.  Try both to find the minimum-dv solution.
static LambertSolution solveLambert(glm::dvec3 r1, glm::dvec3 r2,
                                     double tof_s, double mu, bool use_positive_A)
{
    LambertSolution sol;

    double const r1m = glm::length(r1);
    double const r2m = glm::length(r2);
    if (r1m < 1.0 || r2m < 1.0 || tof_s <= 0.0) return sol;

    double const cos_dnu = std::clamp(glm::dot(r1, r2) / (r1m * r2m), -1.0, 1.0);
    double const A_mag   = std::sqrt(r1m * r2m * (1.0 + cos_dnu));
    if (A_mag < 1.0) return sol;  // degenerate (Δν ≈ π)

    double const A = use_positive_A ? A_mag : -A_mag;

    auto y_of = [&](double z) -> double {
        double C, S;
        stumpffCS(z, C, S);
        if (C < 1e-30) return -1.0;
        return r1m + r2m + A * (z * S - 1.0) / std::sqrt(C);
    };

    double z = 0.0;
    for (int k = 0; k < 1000 && y_of(z) <= 0.0; ++k) z += 0.1;
    if (y_of(z) <= 0.0) return sol;

    for (int iter = 0; iter < 300; ++iter) {
        double C, S;
        stumpffCS(z, C, S);
        double const y = r1m + r2m + A * (z * S - 1.0) / std::sqrt(C);
        if (y <= 0.0) { z += 0.1; continue; }

        double const x = std::sqrt(y / C);
        double const t = (x*x*x * S + A * std::sqrt(y)) / std::sqrt(mu);

        if (std::abs(tof_s - t) < 1.0) {
            double const f     = 1.0 - y / r1m;
            double const g     = A * std::sqrt(y / mu);
            double const gdot  = 1.0 - y / r2m;
            if (std::abs(g) < 1e-10) break;
            sol.v1        = (r2 - f * r1) / g;
            sol.v2        = (gdot * r2 - r1) / g;
            sol.converged = true;
            return sol;
        }

        // Numerical derivative dt/dz for Newton-Raphson
        double const h = (std::abs(z) > 1.0) ? std::abs(z) * 1e-6 : 1e-6;
        double C2, S2;
        stumpffCS(z + h, C2, S2);
        double const y2 = r1m + r2m + A * ((z+h) * S2 - 1.0) / std::sqrt(C2);
        double const x2 = std::sqrt(std::max(y2, 0.0) / C2);
        double const t2 = (x2*x2*x2 * S2 + A * std::sqrt(std::max(y2, 0.0))) / std::sqrt(mu);
        double const dtdz = (t2 - t) / h;

        if (std::abs(dtdz) < 1e-30) break;
        z += std::clamp((tof_s - t) / dtdz, -10.0, 10.0);
        z  = std::clamp(z, -100.0, 200.0);
    }
    return sol;
}

// ── Physics helpers ───────────────────────────────────────────────────────────

static void advance_simulation(CliContext& ctx, double sim_seconds)
{
    auto& ss = ctx.ss;
    if (ss.physics_mode == PhysicsMode::NBody)
        updateSolarSystem(ss, sim_seconds);
    else
        updateSolarSystemPatchedConic(ss, sim_seconds);
    for (std::size_t i = 0; i < ss.spacecraft_states.size(); ++i)
        updateSpacecraftSOI(ss, i);
}

static void dominant_body_state(CliContext const& ctx, int sc_idx,
                                glm::dvec3& dom_pos, glm::dvec3& dom_vel, double& dom_GM)
{
    static constexpr double GM_SUN = 1.32712440018e11;
    static constexpr double G_km   = 6.674e-20;

    auto const& ss = ctx.ss;
    auto const& sc = ss.spacecraft_states[sc_idx];

    dom_pos = glm::dvec3{0.0};
    dom_vel = glm::dvec3{0.0};
    dom_GM  = GM_SUN;

    if (!sc.dominant_is_moon && sc.dominant_body_idx > 0) {
        auto i  = sc.dominant_body_idx;
        dom_pos = ss.states[i].position_km;
        dom_vel = ss.states[i].velocity_km;
        dom_GM  = G_km * ss.defs[i].mass_kg;
    } else if (sc.dominant_is_moon && sc.dominant_moon_idx >= 0) {
        auto mi = sc.dominant_moon_idx;
        dom_pos = ss.moon_states[mi].position_km;
        dom_vel = ss.moon_states[mi].velocity_km;
        int pi  = ss.moon_states[mi].parent_planet_index;
        int mii = ss.moon_states[mi].moon_index;
        dom_GM  = G_km * ss.defs[pi].moons[mii].mass_kg;
    }
}

// ── String parsing helpers ────────────────────────────────────────────────────

static std::string trim(std::string s)
{
    auto not_space = [](unsigned char c){ return !std::isspace(c); };
    s.erase(s.begin(), std::ranges::find_if(s, not_space));
    s.erase(std::ranges::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

static double parse_advance_arg(std::string const& arg)
{
    if (arg.ends_with('d')) return std::stod(arg) * 86400.0;
    if (arg.ends_with('h')) return std::stod(arg) * 3600.0;
    return std::stod(arg);
}

// ── Commands ──────────────────────────────────────────────────────────────────

static void cmd_physics_mode(CliContext& ctx, std::istringstream& args)
{
    std::string mode; args >> mode;
    if (mode == "patched_conic") {
        ctx.ss.physics_mode = PhysicsMode::PatchedConic;
        std::println("PHYSICS_MODE  patched_conic");
    } else if (mode == "nbody") {
        ctx.ss.physics_mode = PhysicsMode::NBody;
        std::println("PHYSICS_MODE  nbody");
    } else {
        std::println(stderr, "ERROR: unknown physics_mode '{}'", mode);
    }
}

static void cmd_advance(CliContext& ctx, std::istringstream& args)
{
    std::string arg; args >> arg;
    double const seconds = parse_advance_arg(arg);
    advance_simulation(ctx, seconds);
    std::println("ADVANCED  sim_seconds={:.1f}  elapsed_s={:.1f}  days_j2000={:.4f}",
                 seconds, ctx.ss.elapsed_simulation_s,
                 ctx.ss.elapsed_simulation_s / 86400.0);
}

static void cmd_date(CliContext& ctx, std::istringstream& args)
{
    std::string date_str; args >> date_str;
    int year{}, month{}, day{};
    if (std::sscanf(date_str.c_str(), "%d-%d-%d", &year, &month, &day) != 3) {
        std::println(stderr, "ERROR: bad date '{}', expected YYYY-MM-DD", date_str);
        return;
    }
    double const target = dateToElapsed(year, month, day);
    double const delta  = target - ctx.ss.elapsed_simulation_s;
    if (delta < 0.0) {
        std::println(stderr, "ERROR: date {} is in the past (elapsed={:.0f} s)",
                     date_str, ctx.ss.elapsed_simulation_s);
        return;
    }
    advance_simulation(ctx, delta);
    std::println("DATE  {}  elapsed_s={:.1f}  days_j2000={:.4f}",
                 date_str, ctx.ss.elapsed_simulation_s,
                 ctx.ss.elapsed_simulation_s / 86400.0);
}

static void cmd_spawn_spacecraft(CliContext& ctx)
{
    auto& ss = ctx.ss;
    spawnSpacecraftAtEarth(ss.spacecraft_defs, ss.spacecraft_states, ss);
    int const idx = static_cast<int>(ss.spacecraft_states.size()) - 1;
    updateSpacecraftSOI(ss, idx);
    auto const& sc  = ss.spacecraft_states[idx];
    std::println("SPAWN_SPACECRAFT  index={}  position_km=({:.3f},{:.3f},{:.3f})",
                 idx, sc.position_km.x, sc.position_km.y, sc.position_km.z);
}

static void cmd_select_spacecraft(CliContext& ctx, std::istringstream& args)
{
    int idx; args >> idx;
    if (idx < 0 || idx >= static_cast<int>(ctx.ss.spacecraft_states.size())) {
        std::println(stderr, "ERROR: spacecraft index {} out of range", idx);
        return;
    }
    ctx.selected_sc = idx;
    std::println("SELECT_SPACECRAFT  index={}", idx);
}

static void cmd_maneuver(CliContext& ctx, std::istringstream& args)
{
    PendingManeuver pm;
    std::string token;
    while (args >> token) {
        auto eq = token.find('=');
        if (eq == std::string::npos) continue;
        std::string const key = token.substr(0, eq);
        double const val = std::stod(token.substr(eq + 1));
        if      (key == "prograde") pm.prograde_dv = val;
        else if (key == "radial")   pm.radial_dv   = val;
        else if (key == "normal")   pm.normal_dv   = val;
        else if (key == "t0")       pm.t0_seconds  = val;
    }
    pm.valid = true;
    ctx.pending = pm;
    std::println("MANEUVER_PENDING  prograde={:.4f}  radial={:.4f}  normal={:.4f}  t0={:.1f}s",
                 pm.prograde_dv, pm.radial_dv, pm.normal_dv, pm.t0_seconds);
}

static void cmd_approve_maneuver(CliContext& ctx)
{
    if (ctx.selected_sc < 0) {
        std::println(stderr, "ERROR: no spacecraft selected");
        return;
    }
    if (!ctx.pending.valid) {
        std::println(stderr, "ERROR: no pending maneuver");
        return;
    }

    if (ctx.pending.t0_seconds > 0.0)
        advance_simulation(ctx, ctx.pending.t0_seconds);

    auto& ss = ctx.ss;
    auto& sc = ss.spacecraft_states[ctx.selected_sc];

    glm::dvec3 dom_pos, dom_vel;
    double dom_GM;
    dominant_body_state(ctx, ctx.selected_sc, dom_pos, dom_vel, dom_GM);

    glm::dvec3 const r_rel = sc.position_km - dom_pos;
    glm::dvec3 const v_rel = sc.velocity_km - dom_vel;
    glm::dvec3 const dv    = dvWorld(r_rel, v_rel,
                                      ctx.pending.prograde_dv,
                                      ctx.pending.radial_dv,
                                      ctx.pending.normal_dv);

    sc.velocity_km += dv;
    updateSpacecraftSOI(ss, ctx.selected_sc);

    ManeuverNode node;
    node.t0_abs_s              = ss.elapsed_simulation_s;
    node.prograde_dv           = ctx.pending.prograde_dv;
    node.radial_dv             = ctx.pending.radial_dv;
    node.normal_dv             = ctx.pending.normal_dv;
    node.approved              = true;
    node.completed             = true;
    node.burn_pos_rel          = r_rel;
    node.burn_vel_rel          = v_rel;
    node.delta_v_world         = dv;
    node.burn_dominant_body_idx = sc.dominant_body_idx;
    node.burn_dominant_is_moon  = sc.dominant_is_moon;
    node.burn_dominant_moon_idx = sc.dominant_moon_idx;
    node.accumulated_dv        = glm::length(dv);
    sc.maneuvers.push_back(node);

    ctx.pending.valid = false;

    std::println("MANEUVER_APPLIED  dv_world=({:.6f},{:.6f},{:.6f})  |dv|={:.4f} km/s",
                 dv.x, dv.y, dv.z, glm::length(dv));
}

// Returns the number of seconds to advance until the spacecraft's LEO velocity
// is maximally aligned with its dominant body's heliocentric velocity.
// Works on a const snapshot — does not advance the simulation.
static double nightside_advance_seconds(CliContext const& ctx)
{
    auto const& sc = ctx.ss.spacecraft_states[ctx.selected_sc];
    glm::dvec3 dom_pos, dom_vel; double dom_GM;
    dominant_body_state(ctx, ctx.selected_sc, dom_pos, dom_vel, dom_GM);
    glm::dvec3 const r_rel = sc.position_km - dom_pos;
    glm::dvec3 const v_rel = sc.velocity_km - dom_vel;
    double const omega = glm::length(v_rel) / glm::length(r_rel);
    glm::dvec3 const r_hat       = glm::normalize(r_rel);
    glm::dvec3 const v_hat       = glm::normalize(v_rel);
    glm::dvec3 const dom_vel_hat = (glm::length(dom_vel) > 1e-10)
                                      ? glm::normalize(dom_vel) : v_hat;
    // v(θ) = v_circ*(−sin θ * r̂₀ + cos θ * v̂₀); maximise dot(v, dom_vel):
    // f'(θ)=0  →  tan θ = −dr/dv  →  θ = atan2(−dr, dv)
    double theta = std::atan2(-glm::dot(r_hat, dom_vel_hat),
                               glm::dot(v_hat, dom_vel_hat));
    if (theta < 0.0) theta += 2.0 * std::numbers::pi_v<double>;
    return theta / omega;
}

static void cmd_advance_to_nightside(CliContext& ctx)
{
    if (ctx.selected_sc < 0) {
        std::println(stderr, "ERROR: no spacecraft selected");
        return;
    }
    auto const& sc = ctx.ss.spacecraft_states[ctx.selected_sc];
    glm::dvec3 dom_pos, dom_vel; double dom_GM;
    dominant_body_state(ctx, ctx.selected_sc, dom_pos, dom_vel, dom_GM);
    glm::dvec3 const r_rel = sc.position_km - dom_pos;
    glm::dvec3 const v_rel = sc.velocity_km - dom_vel;
    double const omega = glm::length(v_rel) / glm::length(r_rel);
    double const T_orb = 2.0 * std::numbers::pi_v<double> / omega;

    double const advance_s = nightside_advance_seconds(ctx);
    advance_simulation(ctx, advance_s);
    std::println("ADVANCE_TO_NIGHTSIDE  advance_s={:.1f}  fraction_of_orbit={:.3f}",
                 advance_s, advance_s / T_orb);
}

// Compute the optimal departure burn from a circular parking orbit to achieve
// a given hyperbolic excess velocity v_inf (in parent-body frame).
//
// At periapsis of the departure hyperbola, the periapsis velocity direction is
//   v_hat_dep = normalize(v_inf - dot(v_inf, r_hat_dep) * r_hat_dep)
// i.e. the component of v_inf perpendicular to the current radial direction.
// The burn dv = v_hyp*v_hat_dep - v_circ*v_hat_circ has no radial component
// (both v_hat_dep and v_hat_circ are perpendicular to r_hat_dep).
// We scan theta in [0, 2π) to find the periapsis angle that minimises |dv|.
struct DepartureOpt {
    double advance_s;
    double prograde_dv;
    double normal_dv;
    double dv_total;
};

static DepartureOpt computeDepartureBurn(
    glm::dvec3 const& r_hat_0, glm::dvec3 const& v_hat_0,
    double omega, double v_circ, double GM_parent, double R_LEO,
    glm::dvec3 const& v_inf)
{
    double const v_inf_m = glm::length(v_inf);
    double const v_esc   = std::sqrt(2.0 * GM_parent / R_LEO);
    double const v_hyp   = std::sqrt(v_esc*v_esc + v_inf_m*v_inf_m);
    glm::dvec3 const n_orbit = glm::normalize(glm::cross(r_hat_0, v_hat_0));

    auto eval = [&](double theta) -> double {
        glm::dvec3 const r_hat = std::cos(theta)*r_hat_0 + std::sin(theta)*v_hat_0;
        glm::dvec3 const v_hat = -std::sin(theta)*r_hat_0 + std::cos(theta)*v_hat_0;
        glm::dvec3 const v_perp = v_inf - glm::dot(v_inf, r_hat) * r_hat;
        if (glm::length(v_perp) < 1e-10) return 1e30;
        glm::dvec3 const dv = v_hyp * glm::normalize(v_perp) - v_circ * v_hat;
        return glm::dot(dv, dv);
    };

    // Coarse scan
    int const N = 3600;
    double best_theta = 0.0;
    double best_val   = eval(0.0);
    for (int k = 1; k < N; ++k) {
        double const theta = k * 2.0 * std::numbers::pi_v<double> / N;
        double const val = eval(theta);
        if (val < best_val) { best_val = val; best_theta = theta; }
    }

    // Ternary-search refinement
    double lo = best_theta - 2.0 * std::numbers::pi_v<double> / N;
    double hi = best_theta + 2.0 * std::numbers::pi_v<double> / N;
    for (int i = 0; i < 60; ++i) {
        double const m1 = lo + (hi - lo) / 3.0;
        double const m2 = hi - (hi - lo) / 3.0;
        if (eval(m1) < eval(m2)) hi = m2; else lo = m1;
    }
    best_theta = (lo + hi) / 2.0;
    if (best_theta < 0.0) best_theta += 2.0 * std::numbers::pi_v<double>;

    glm::dvec3 const r_hat  = glm::normalize(std::cos(best_theta)*r_hat_0 + std::sin(best_theta)*v_hat_0);
    glm::dvec3 const v_hat  = glm::normalize(-std::sin(best_theta)*r_hat_0 + std::cos(best_theta)*v_hat_0);
    glm::dvec3 const v_perp = v_inf - glm::dot(v_inf, r_hat) * r_hat;
    glm::dvec3 const dv     = v_hyp * glm::normalize(v_perp) - v_circ * v_hat;

    DepartureOpt opt;
    opt.advance_s   = best_theta / omega;
    opt.prograde_dv = glm::dot(dv, v_hat);
    opt.normal_dv   = glm::dot(dv, n_orbit);
    opt.dv_total    = glm::length(dv);
    return opt;
}

static void cmd_find_transfer(CliContext& ctx, std::istringstream& args)
{
    if (ctx.selected_sc < 0) {
        std::println(stderr, "ERROR: no spacecraft selected");
        return;
    }

    std::string target_name;
    args >> target_name;

    double min_tof_days = 100.0;
    double max_tof_days = 600.0;
    {
        std::string token;
        while (args >> token) {
            auto eq = token.find('=');
            if (eq == std::string::npos) continue;
            std::string const key = token.substr(0, eq);
            double const val = std::stod(token.substr(eq + 1));
            if      (key == "min_tof") min_tof_days = val;
            else if (key == "max_tof") max_tof_days = val;
        }
    }

    int target_idx = -1;
    for (std::size_t i = 0; i < ctx.ss.defs.size(); ++i)
        if (ctx.ss.defs[i].name == target_name) { target_idx = static_cast<int>(i); break; }
    if (target_idx < 0) {
        std::println(stderr, "ERROR: planet '{}' not found", target_name);
        return;
    }

    int earth_idx = -1;
    for (std::size_t i = 0; i < ctx.ss.defs.size(); ++i)
        if (ctx.ss.defs[i].name == "Earth") { earth_idx = static_cast<int>(i); break; }

    static constexpr double GM_SUN = 1.32712440018e11;
    static constexpr double G_km   = 6.674e-20;

    glm::dvec3 const r1      = ctx.ss.states[earth_idx].position_km;
    glm::dvec3 const v_earth = ctx.ss.states[earth_idx].velocity_km;
    glm::dvec3 const r_tgt0  = ctx.ss.states[target_idx].position_km;
    glm::dvec3 const v_tgt0  = ctx.ss.states[target_idx].velocity_km;

    auto const& sc    = ctx.ss.spacecraft_states[ctx.selected_sc];
    glm::dvec3 const r_rel0  = sc.position_km - r1;
    glm::dvec3 const v_rel0  = sc.velocity_km - v_earth;
    glm::dvec3 const r_hat_0 = glm::normalize(r_rel0);
    glm::dvec3 const v_hat_0 = glm::normalize(v_rel0);
    double     const omega   = glm::length(v_rel0) / glm::length(r_rel0);

    double const GM_earth = G_km * ctx.ss.defs[earth_idx].mass_kg;
    double const R_LEO    = ctx.ss.defs[earth_idx].radius_km + 400.0;
    double const v_circ   = std::sqrt(GM_earth / R_LEO);
    double const v_esc    = std::sqrt(2.0 * GM_earth / R_LEO);

    double     best_dv        = std::numeric_limits<double>::max();
    double     best_tof_s     = 0.0;
    double     best_v_inf_m   = 0.0;
    double     best_advance_s = 0.0;
    double     best_prograde  = 0.0;
    double     best_normal    = 0.0;
    double     best_oberth    = 0.0;
    glm::dvec3 best_v1{};
    glm::dvec3 best_r2{};

    bool const verbose = (max_tof_days - min_tof_days) <= 100.0;
    if (verbose) std::println("  tof_days   v_inf   dv_actual  branch  prograde");

    for (double tof_days = min_tof_days; tof_days <= max_tof_days; tof_days += 1.0) {
        double const tof_s = tof_days * 86400.0;
        auto const [r2, v2_tgt] = keplerPropagate(r_tgt0, v_tgt0, GM_SUN, tof_s);

        for (bool pos_A : {true, false}) {
            LambertSolution const sol = solveLambert(r1, r2, tof_s, GM_SUN, pos_A);
            if (!sol.converged) continue;
            bool const is_prograde = (glm::cross(r1, sol.v1).y < 0.0);
            if (!is_prograde) continue;

            glm::dvec3 const v_inf_vec = sol.v1 - v_earth;
            double     const v_inf_m   = glm::length(v_inf_vec);

            DepartureOpt const opt = computeDepartureBurn(
                r_hat_0, v_hat_0, omega, v_circ, GM_earth, R_LEO, v_inf_vec);

            if (verbose) std::println("  {:.0f}  {:.4f}  {:.4f}  {}  YES",
                tof_days, v_inf_m, opt.dv_total, pos_A ? "+" : "-");

            if (opt.dv_total < best_dv) {
                best_dv        = opt.dv_total;
                best_tof_s     = tof_s;
                best_v_inf_m   = v_inf_m;
                best_advance_s = opt.advance_s;
                best_prograde  = opt.prograde_dv;
                best_normal    = opt.normal_dv;
                best_oberth    = std::sqrt(v_esc*v_esc + v_inf_m*v_inf_m) - v_circ;
                best_v1        = sol.v1;
                best_r2        = r2;
            }
        }
    }

    if (best_dv == std::numeric_limits<double>::max()) {
        std::println(stderr, "ERROR: no convergent Lambert solution found");
        return;
    }

    {
        auto const [r_check, v_check] = keplerPropagate(r1, best_v1, GM_SUN, best_tof_s);
        double const err_km = glm::length(r_check - best_r2);
        glm::dvec3 const v_inf_vec = best_v1 - v_earth;
        std::println("  [verify] lambert_arrival_error_km  {:.1f}  ({:.6f} AU)",
            err_km, err_km / 1.496e8);
        std::println("  [verify] v1_lambert = ({:.4f}, {:.4f}, {:.4f}) km/s  |v1|={:.4f}",
            best_v1.x, best_v1.y, best_v1.z, glm::length(best_v1));
        std::println("  [verify] v_inf_vec  = ({:.4f}, {:.4f}, {:.4f}) km/s",
            v_inf_vec.x, v_inf_vec.y, v_inf_vec.z);
        std::println("  [verify] v_earth    = ({:.4f}, {:.4f}, {:.4f}) km/s  |ve|={:.4f}",
            v_earth.x, v_earth.y, v_earth.z, glm::length(v_earth));
    }

    double const dep_days = ctx.ss.elapsed_simulation_s / 86400.0 + best_advance_s / 86400.0;
    double const arr_days = dep_days + best_tof_s / 86400.0;

    std::println("FIND_TRANSFER  target={}", target_name);
    std::println("  dep_days_j2000      {:.4f}", dep_days);
    std::println("  tof_days            {:.4f}", best_tof_s / 86400.0);
    std::println("  arr_days_j2000      {:.4f}", arr_days);
    std::println("  v_inf_km_s          {:.4f}", best_v_inf_m);
    std::println("  min_dv_oberth_km_s  {:.4f}", best_oberth);
    std::println("  actual_dv_km_s      {:.4f}", best_dv);
    std::println("  advance_s           {:.1f}", best_advance_s);
    std::println("  prograde_dv         {:.4f}", best_prograde);
    std::println("  radial_dv           0.0000");
    std::println("  normal_dv           {:.4f}", best_normal);
    std::println("  soi_radius_km       {:.1f}", ctx.ss.defs[target_idx].soi_km);
    std::println("  ---");
    std::println("  advance {:.1f}", best_advance_s);
    std::println("  maneuver prograde={:.4f} radial=0.0000 normal={:.4f} t0=0",
                 best_prograde, best_normal);
}

static void cmd_report_state(CliContext& ctx)
{
    if (ctx.selected_sc < 0) {
        std::println(stderr, "ERROR: no spacecraft selected");
        return;
    }
    auto const& ss = ctx.ss;
    auto const& sc = ss.spacecraft_states[ctx.selected_sc];

    glm::dvec3 dom_pos, dom_vel;
    double dom_GM;
    dominant_body_state(ctx, ctx.selected_sc, dom_pos, dom_vel, dom_GM);

    glm::dvec3 const r_rel = sc.position_km - dom_pos;
    glm::dvec3 const v_rel = sc.velocity_km - dom_vel;
    OsculatingOrbit const orb = computeOsculatingOrbit(r_rel, v_rel, dom_GM);

    std::string dom_name = "Sun";
    if (!sc.dominant_is_moon && sc.dominant_body_idx > 0)
        dom_name = ss.defs[sc.dominant_body_idx].name;
    else if (sc.dominant_is_moon && sc.dominant_moon_idx >= 0)
        dom_name = ss.defs[ss.moon_states[sc.dominant_moon_idx].parent_planet_index]
                       .moons[ss.moon_states[sc.dominant_moon_idx].moon_index].name;

    std::println("STATE  elapsed_s={:.1f}  days_j2000={:.4f}",
                 ss.elapsed_simulation_s, ss.elapsed_simulation_s / 86400.0);
    std::println("  position_km         ({:.3f},{:.3f},{:.3f})",
                 sc.position_km.x, sc.position_km.y, sc.position_km.z);
    std::println("  velocity_km_s       ({:.6f},{:.6f},{:.6f})",
                 sc.velocity_km.x, sc.velocity_km.y, sc.velocity_km.z);
    std::println("  dominant_body       {}", dom_name);
    std::println("  semi_major_axis_km  {:.1f}", orb.a);
    std::println("  eccentricity        {:.6f}", orb.e);
    std::println("  periapsis_km        {:.1f}", orb.periapsis_km());
    std::println("  apoapsis_km         {:.1f}", orb.e < 1.0 ? orb.apoapsis_km() : -1.0);
}

static void cmd_report_closest_approach(CliContext& ctx, std::istringstream& args)
{
    std::string planet_name;
    double scan_days = 400.0;
    args >> planet_name;
    std::string extra;
    while (args >> extra) {
        auto eq = extra.find('=');
        std::string const val_str = (eq != std::string::npos) ? extra.substr(eq + 1) : extra;
        if (extra.starts_with("days")) scan_days = std::stod(val_str);
        else                           scan_days = std::stod(val_str);
    }

    if (ctx.selected_sc < 0) {
        std::println(stderr, "ERROR: no spacecraft selected");
        return;
    }

    // Find planet index
    int planet_idx = -1;
    for (std::size_t i = 0; i < ctx.ss.defs.size(); ++i)
        if (ctx.ss.defs[i].name == planet_name) { planet_idx = static_cast<int>(i); break; }
    if (planet_idx < 0) {
        std::println(stderr, "ERROR: planet '{}' not found", planet_name);
        return;
    }

    double const soi_km      = ctx.ss.defs[planet_idx].soi_km;
    double const scan_total  = scan_days * 86400.0;
    double const coarse_step = 3600.0;

    // Phase 1: coarse scan on a value-copy
    SolarSystem ss_scan = ctx.ss;
    double min_dist        = std::numeric_limits<double>::max();
    double elapsed_at_min  = ss_scan.elapsed_simulation_s;
    SolarSystem ss_chk     = ss_scan;  // checkpoint before current minimum

    double elapsed_scan = 0.0;
    while (elapsed_scan < scan_total) {
        double const step = std::min(coarse_step, scan_total - elapsed_scan);
        if (ss_scan.physics_mode == PhysicsMode::NBody)
            updateSolarSystem(ss_scan, step);
        else
            updateSolarSystemPatchedConic(ss_scan, step);
        updateSpacecraftSOI(ss_scan, ctx.selected_sc);
        elapsed_scan += step;

        glm::dvec3 const& sc_pos = ss_scan.spacecraft_states[ctx.selected_sc].position_km;
        glm::dvec3 const& pl_pos = ss_scan.states[planet_idx].position_km;
        double const dist = glm::length(sc_pos - pl_pos);

        if (dist < min_dist) {
            ss_chk        = ss_scan;                    // snapshot one step before minimum
            min_dist      = dist;
            elapsed_at_min = ss_scan.elapsed_simulation_s;
        }
    }

    // Phase 2: ternary search within ±2 steps from checkpoint
    double const refine_window = 2.0 * coarse_step;
    double lo = 0.0;
    double hi = refine_window;

    auto dist_at = [&](double offset) -> double {
        SolarSystem ss_tmp = ss_chk;
        if (ss_tmp.physics_mode == PhysicsMode::NBody)
            updateSolarSystem(ss_tmp, offset);
        else
            updateSolarSystemPatchedConic(ss_tmp, offset);
        return glm::length(
            ss_tmp.spacecraft_states[ctx.selected_sc].position_km -
            ss_tmp.states[planet_idx].position_km);
    };

    for (int iter = 0; iter < 60 && (hi - lo) > 1.0; ++iter) {
        double const m1 = lo + (hi - lo) / 3.0;
        double const m2 = hi - (hi - lo) / 3.0;
        if (dist_at(m1) < dist_at(m2)) hi = m2;
        else                            lo = m1;
    }

    double const refined_offset = (lo + hi) / 2.0;

    SolarSystem ss_final = ss_chk;
    if (ss_final.physics_mode == PhysicsMode::NBody)
        updateSolarSystem(ss_final, refined_offset);
    else
        updateSolarSystemPatchedConic(ss_final, refined_offset);

    double const refined_dist  = glm::length(
        ss_final.spacecraft_states[ctx.selected_sc].position_km -
        ss_final.states[planet_idx].position_km);
    double const elapsed_final = ss_final.elapsed_simulation_s;
    double const days_from_now = (elapsed_final - ctx.ss.elapsed_simulation_s) / 86400.0;

    std::println("CLOSEST_APPROACH  target={}", planet_name);
    std::println("  min_distance_km     {:.1f}", refined_dist);
    std::println("  min_distance_AU     {:.6f}", refined_dist / 1.496e8);
    std::println("  elapsed_s           {:.1f}", elapsed_final);
    std::println("  days_from_j2000     {:.4f}", elapsed_final / 86400.0);
    std::println("  days_from_now       {:.4f}", days_from_now);
    std::println("  soi_radius_km       {:.1f}", soi_km);
    std::println("  soi_entered         {}", refined_dist < soi_km ? "YES" : "NO");
}

static void cmd_report(CliContext& ctx, std::istringstream& args)
{
    std::string sub; args >> sub;
    if      (sub == "state")             cmd_report_state(ctx);
    else if (sub == "closest_approach")  cmd_report_closest_approach(ctx, args);
    else    std::println(stderr, "ERROR: unknown report sub-command '{}'", sub);
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    spdlog::set_level(spdlog::level::warn);  // suppress physics info logs

    if (argc < 2) {
        std::println(stderr, "Usage: physics-cli <script.phys>");
        return 1;
    }

    std::ifstream f(argv[1]);
    if (!f) {
        std::println(stderr, "ERROR: cannot open '{}'", argv[1]);
        return 1;
    }

    CliContext ctx;
    ctx.ss = createSolarSystem();

    std::string line;
    int line_num = 0;
    while (std::getline(f, line)) {
        ++line_num;
        // strip comments
        if (auto pos = line.find('#'); pos != std::string::npos)
            line = line.substr(0, pos);
        line = trim(line);
        if (line.empty()) continue;

        std::istringstream tokens(line);
        std::string cmd; tokens >> cmd;

        if      (cmd == "physics_mode")         cmd_physics_mode(ctx, tokens);
        else if (cmd == "advance")              cmd_advance(ctx, tokens);
        else if (cmd == "date")                 cmd_date(ctx, tokens);
        else if (cmd == "spawn_spacecraft")     cmd_spawn_spacecraft(ctx);
        else if (cmd == "select_spacecraft")    cmd_select_spacecraft(ctx, tokens);
        else if (cmd == "advance_to_nightside") cmd_advance_to_nightside(ctx);
        else if (cmd == "find_transfer")        cmd_find_transfer(ctx, tokens);
        else if (cmd == "maneuver")             cmd_maneuver(ctx, tokens);
        else if (cmd == "approve_maneuver")     cmd_approve_maneuver(ctx);
        else if (cmd == "report")               cmd_report(ctx, tokens);
        else std::println(stderr, "ERROR (line {}): unknown command '{}'", line_num, cmd);
    }

    return 0;
}
