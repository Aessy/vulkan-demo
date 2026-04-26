#include "PatchedConic.h"
#include "Maneuver.h"
#include "Spacecraft.h"

#include <glm/gtc/quaternion.hpp>
#include <numbers>
#include <ranges>

using glm::dvec3;

static constexpr double GM_SUN_PC = 1.32712440018e11; // km³/s²
static constexpr double G_km_PC   = 6.674e-20;        // km³/(kg·s²)

struct DomState { dvec3 pos_begin, vel_begin, pos_end, vel_end; double GM; };

// Returns the dominant body's state at the start (ab=0) and end (ae=1) of the frame.
// Planets and moons have already had prev_* saved and position/velocity advanced this frame,
// so ab=0 → prev (start of frame), ae=1 → current (end of frame).
static DomState dominantBodyState(SolarSystem const& ss, SpacecraftState const& sc)
{
    if (sc.dominant_is_moon)
    {
        auto const& ms  = ss.moon_states[sc.dominant_moon_idx];
        auto const& def = ss.defs[ms.parent_planet_index].moons[ms.moon_index];
        return { ms.prev_position_km, ms.prev_velocity_km,
                 ms.position_km,      ms.velocity_km,
                 G_km_PC * def.mass_kg };
    }
    if (sc.dominant_body_idx == 0)
        return { dvec3(0.0), dvec3(0.0), dvec3(0.0), dvec3(0.0), GM_SUN_PC };

    auto const& ps = ss.states[sc.dominant_body_idx];
    return { ps.prev_position_km, ps.prev_velocity_km,
             ps.position_km,      ps.velocity_km,
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

        updateSpacecraftSOI(ss, i);
        auto const  d     = dominantBodyState(ss, sc);
        dvec3 const r_rel = sc.position_km - d.pos_begin;
        dvec3 const v_rel = sc.velocity_km - d.vel_begin + thrust_a * scaled_dt;
        auto [r_new, v_new] = keplerPropagate(r_rel, v_rel, d.GM, scaled_dt);
        sc.position_km = r_new + d.pos_end;
        sc.velocity_km = v_new + d.vel_end;
        sc.time_accumulator = 0.0;
    }
}

void updateSolarSystemPatchedConic(SolarSystem& ss, double delta_seconds)
{
    double const scaled_dt = delta_seconds * ss.time_scale;
    ss.elapsed_simulation_s += scaled_dt;

    // Planets: one exact Kepler step per frame — keplerPropagate is analytic for any dt.
    for (auto&& [def, state] : std::views::zip(ss.defs, ss.states))
    {
        state.prev_position_km    = state.position_km;
        state.prev_velocity_km    = state.velocity_km;
        state.prev_rotation_angle = state.rotation_angle;

        if (def.semi_major_axis_km <= 0.0) continue;

        auto [r, v] = keplerPropagate(state.position_km, state.velocity_km, GM_SUN_PC, scaled_dt);
        state.position_km = r;
        state.velocity_km = v;

        if (def.rotation_period_s > 0.0)
            state.rotation_angle +=
                (2.0 * std::numbers::pi_v<double> / def.rotation_period_s) * scaled_dt;
    }

    // Moons: one exact Kepler step per frame around their parent planet.
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

        auto [r_new, v_new] = keplerPropagate(r_rel, v_rel, GM_parent, scaled_dt);

        moon.position_km = r_new + parent_state.position_km;
        moon.velocity_km = v_new + parent_state.velocity_km;

        if (moon_def.rotation_period_s > 0.0)
            moon.rotation_angle +=
                (2.0 * std::numbers::pi_v<double> / moon_def.rotation_period_s) * scaled_dt;
    }

    // render_alpha = 1: planets/moons are at their end-of-frame positions.
    ss.render_alpha = 1.0;

    // Spacecraft: one exact Kepler step per frame using planet prev→pos as the reference frame.
    updateSpacecraftsPatchedConic(ss, scaled_dt);
}
