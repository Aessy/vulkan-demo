#include "PatchedConic.h"
#include "Maneuver.h"
#include "Spacecraft.h"

#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <numbers>
#include <ranges>

using glm::dvec3;

static constexpr double GM_SUN_PC = 1.32712440018e11; // km³/s²
static constexpr double G_km_PC   = 6.674e-20;        // km³/(kg·s²)
static constexpr double PLANET_DT = 3600.0;           // planet/moon fixed step (s)
static constexpr double SC_DT_PC  = 1.0;              // spacecraft coasting substep (s)

struct DomState { dvec3 pos_begin, vel_begin, pos_end, vel_end; double GM; };

static DomState dominantBodyState(SolarSystem const& ss, SpacecraftState const& sc,
                                  double ab, double ae)
{
    if (sc.dominant_is_moon)
    {
        auto const& ms  = ss.moon_states[sc.dominant_moon_idx];
        auto const& def = ss.defs[ms.parent_planet_index].moons[ms.moon_index];
        return { glm::mix(ms.prev_position_km, ms.position_km, ab),
                 glm::mix(ms.prev_velocity_km, ms.velocity_km, ab),
                 glm::mix(ms.prev_position_km, ms.position_km, ae),
                 glm::mix(ms.prev_velocity_km, ms.velocity_km, ae),
                 G_km_PC * def.mass_kg };
    }
    if (sc.dominant_body_idx == 0)
        return { dvec3(0.0), dvec3(0.0), dvec3(0.0), dvec3(0.0), GM_SUN_PC };

    auto const& ps = ss.states[sc.dominant_body_idx];
    return { glm::mix(ps.prev_position_km, ps.position_km, ab),
             glm::mix(ps.prev_velocity_km, ps.velocity_km, ab),
             glm::mix(ps.prev_position_km, ps.position_km, ae),
             glm::mix(ps.prev_velocity_km, ps.velocity_km, ae),
             G_km_PC * ss.defs[sc.dominant_body_idx].mass_kg };
}

static void updateSpacecraftsPatchedConic(SolarSystem& ss, double scaled_dt)
{
    for (std::size_t i = 0; i < ss.spacecraft_defs.size(); ++i)
    {
        auto const& def = ss.spacecraft_defs[i];
        auto&       sc  = ss.spacecraft_states[i];

        sc.prev_position_km = sc.position_km;

        dvec3 const fwd      = dvec3(glm::mat3_cast(glm::quat(sc.orientation)) * glm::vec3(0, 1, 0));
        dvec3 const thrust_a = fwd * (sc.thrust_level * def.thrust_N / def.mass_kg * 1e-3);

        // Apply impulse (zero when not thrusting) then propagate as exact Kepler orbit.
        auto step = [&](double dt, double ab, double ae)
        {
            auto const  d     = dominantBodyState(ss, sc, ab, ae);
            dvec3 const r_rel = sc.position_km - d.pos_begin;
            dvec3 const v_rel = sc.velocity_km - d.vel_begin + thrust_a * dt;
            auto [r_new, v_new] = keplerPropagate(r_rel, v_rel, d.GM, dt);
            sc.position_km = r_new + d.pos_end;
            sc.velocity_km = v_new + d.vel_end;
        };

        if (sc.thrust_level > 0.0)
        {
            // During thrust: one step per frame so the orbit ring updates smoothly.
            updateSpacecraftSOI(ss, i);
            double const ab = std::clamp((ss.simulation_time_s - scaled_dt) / PLANET_DT, 0.0, 1.0);
            double const ae = std::clamp( ss.simulation_time_s              / PLANET_DT, 0.0, 1.0);
            step(scaled_dt, ab, ae);
        }
        else
        {
            // Coasting: 1 s substeps keep time_accumulator in [0,1) so the visual
            // position extrapolation in interpolatedSpacecraftPosition stays small.
            sc.time_accumulator += scaled_dt;
            while (sc.time_accumulator >= SC_DT_PC)
            {
                updateSpacecraftSOI(ss, i);
                double const ab = std::clamp((ss.simulation_time_s - sc.time_accumulator)            / PLANET_DT, 0.0, 1.0);
                double const ae = std::clamp((ss.simulation_time_s - sc.time_accumulator + SC_DT_PC) / PLANET_DT, 0.0, 1.0);
                step(SC_DT_PC, ab, ae);
                sc.time_accumulator -= SC_DT_PC;
            }
        }
    }
}

static void advancePlanetsMoons(SolarSystem& ss)
{
    for (auto&& [def, state] : std::views::zip(ss.defs, ss.states))
    {
        state.prev_position_km    = state.position_km;
        state.prev_velocity_km    = state.velocity_km;
        state.prev_rotation_angle = state.rotation_angle;

        if (def.semi_major_axis_km <= 0.0) continue;

        auto [r, v] = keplerPropagate(state.position_km, state.velocity_km, GM_SUN_PC, PLANET_DT);
        state.position_km = r;
        state.velocity_km = v;

        if (def.rotation_period_s > 0.0)
            state.rotation_angle +=
                (2.0 * std::numbers::pi_v<double> / def.rotation_period_s) * PLANET_DT;
    }

    for (auto& moon : ss.moon_states)
    {
        moon.prev_position_km    = moon.position_km;
        moon.prev_velocity_km    = moon.velocity_km;
        moon.prev_rotation_angle = moon.rotation_angle;

        auto const& parent_state = ss.states[moon.parent_planet_index];
        auto const& parent_def   = ss.defs[moon.parent_planet_index];
        auto const& moon_def     = parent_def.moons[moon.moon_index];
        double const GM_parent   = G_km_PC * parent_def.mass_kg;

        dvec3 const r_rel = moon.prev_position_km - parent_state.prev_position_km;
        dvec3 const v_rel = moon.prev_velocity_km  - parent_state.prev_velocity_km;

        auto [r_new, v_new] = keplerPropagate(r_rel, v_rel, GM_parent, PLANET_DT);

        moon.position_km = r_new + parent_state.position_km;
        moon.velocity_km = v_new + parent_state.velocity_km;

        if (moon_def.rotation_period_s > 0.0)
            moon.rotation_angle +=
                (2.0 * std::numbers::pi_v<double> / moon_def.rotation_period_s) * PLANET_DT;
    }
}

void updateSolarSystemPatchedConic(SolarSystem& ss, double delta_seconds)
{
    double remaining = delta_seconds * ss.time_scale;
    ss.elapsed_simulation_s += remaining;

    // Interleave spacecraft substeps with planet steps so spacecraft integration
    // never crosses a planet-step boundary. Crossing would cause a discontinuous
    // jump in Earth-relative position (magnitude ~ |dP_earth| * fraction_of_step_missed),
    // which changes the computed orbital energy and causes orbit drift.
    while (remaining > 0.0)
    {
        double const dt_to_boundary = PLANET_DT - ss.simulation_time_s;
        double const chunk          = std::min(remaining, dt_to_boundary);

        ss.simulation_time_s += chunk;
        remaining            -= chunk;

        updateSpacecraftsPatchedConic(ss, chunk);

        if (ss.simulation_time_s >= PLANET_DT - 1e-9)
        {
            ss.simulation_time_s -= PLANET_DT;
            advancePlanetsMoons(ss);
        }
    }

    ss.render_alpha = ss.simulation_time_s / PLANET_DT;
}
