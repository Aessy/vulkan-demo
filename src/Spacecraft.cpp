#include "Spacecraft.h"
#include "SolarSystem.h"

#include <glm/gtc/quaternion.hpp>
#include <numbers>
#include <algorithm>
#include <cmath>

#include <spdlog/spdlog.h>

using glm::dvec3;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// Gravitational constant in km^3 / (kg * s^2)
static constexpr double G_km = 6.674e-20;

// ---------------------------------------------------------------------------
// Box mesh (2 × 4 × 0.5, local +Y = nose direction)
// ---------------------------------------------------------------------------

static void addFace(Model& m,
                    glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, glm::vec3 v3,
                    glm::vec3 normal)
{
    glm::vec3 t = glm::normalize(v1 - v0);
    glm::vec3 b = glm::normalize(v3 - v0);
    auto base = static_cast<uint32_t>(m.vertices.size());

    m.vertices.push_back({v0, {0.0f, 0.0f}, normal, {0.0f, 0.0f}, t, b});
    m.vertices.push_back({v1, {1.0f, 0.0f}, normal, {1.0f, 0.0f}, t, b});
    m.vertices.push_back({v2, {1.0f, 1.0f}, normal, {1.0f, 1.0f}, t, b});
    m.vertices.push_back({v3, {0.0f, 1.0f}, normal, {0.0f, 1.0f}, t, b});

    m.indices.push_back(base);     m.indices.push_back(base + 1); m.indices.push_back(base + 2);
    m.indices.push_back(base);     m.indices.push_back(base + 2); m.indices.push_back(base + 3);
}

Model createBoxMesh()
{
    // Half-extents — proportions: 5m wide × 30m tall × 5m deep.
    // Scaled at render time by visual_scale_km where 1 unit = 5m.
    constexpr float hx = 0.5f;   // width  / 2  → 1 unit wide
    constexpr float hy = 3.0f;   // length / 2  → 6 units tall  (+Y = forward/nose)
    constexpr float hz = 0.5f;   // depth  / 2  → 1 unit deep

    Model m;

    // +Y face (nose)
    addFace(m,
        {-hx, +hy, -hz}, {+hx, +hy, -hz}, {+hx, +hy, +hz}, {-hx, +hy, +hz},
        {0, 1, 0});

    // -Y face (engine/rear)
    addFace(m,
        {+hx, -hy, -hz}, {-hx, -hy, -hz}, {-hx, -hy, +hz}, {+hx, -hy, +hz},
        {0, -1, 0});

    // +X face (right)
    addFace(m,
        {+hx, -hy, -hz}, {+hx, +hy, -hz}, {+hx, +hy, +hz}, {+hx, -hy, +hz},
        {1, 0, 0});

    // -X face (left)
    addFace(m,
        {-hx, +hy, -hz}, {-hx, -hy, -hz}, {-hx, -hy, +hz}, {-hx, +hy, +hz},
        {-1, 0, 0});

    // +Z face (top)
    addFace(m,
        {-hx, -hy, +hz}, {+hx, -hy, +hz}, {+hx, +hy, +hz}, {-hx, +hy, +hz},
        {0, 0, 1});

    // -Z face (bottom)
    addFace(m,
        {+hx, -hy, -hz}, {-hx, -hy, -hz}, {-hx, +hy, -hz}, {+hx, +hy, -hz},
        {0, 0, -1});

    return m;
}

// ---------------------------------------------------------------------------
// Physics
// ---------------------------------------------------------------------------

// N-body gravitational acceleration at pos from all solar system bodies.
// Uses render_alpha to interpolate planet positions between their discrete
// 3600-second steps. Without this, a planet step causes a 107,000 km jump
// in Earth's position, which sends any nearby spacecraft flying off-orbit.
static dvec3 gravAccel(dvec3 const& pos, SolarSystem const& ss)
{
    dvec3 a{0.0};
    for (std::size_t i = 0; i < ss.defs.size(); ++i)
    {
        dvec3 planet_pos = glm::mix(ss.states[i].prev_position_km,
                                    ss.states[i].position_km,
                                    ss.render_alpha);
        dvec3  r     = planet_pos - pos;
        double r_mag = glm::length(r);
        if (r_mag < ss.defs[i].radius_km) continue; // inside the body, skip
        double GM = G_km * ss.defs[i].mass_kg;
        a += (GM / (r_mag * r_mag * r_mag)) * r;
    }
    return a;
}

// One leapfrog KDK step for a single spacecraft.
static void stepSpacecraft(SpacecraftDef const& def, SpacecraftState& sc,
                           SolarSystem const& ss, double dt)
{
    // Local +Y is the nose direction
    glm::vec3  fwd_f = glm::mat3_cast(glm::quat(sc.orientation)) * glm::vec3(0.0f, 1.0f, 0.0f);
    dvec3      fwd   = glm::dvec3(fwd_f);

    // Thrust acceleration in km/s^2  (F/m in m/s^2, then * 1e-3 → km/s^2)
    dvec3 thrust_a = fwd * (sc.thrust_level * def.thrust_N / def.mass_kg * 1e-3);

    dvec3 acc0 = gravAccel(sc.position_km, ss) + thrust_a;

    dvec3 vel_half    = sc.velocity_km + acc0 * (dt * 0.5);
    sc.position_km   += vel_half * dt;

    dvec3 acc1        = gravAccel(sc.position_km, ss) + thrust_a;
    sc.velocity_km    = vel_half + acc1 * (dt * 0.5);
}

void updateSpacecrafts(std::vector<SpacecraftDef> const& defs,
                       std::vector<SpacecraftState>&      states,
                       SolarSystem const&                 ss,
                       double                             scaled_dt)
{
    static int debug_frame = 0;

    for (std::size_t i = 0; i < defs.size(); ++i)
    {
        auto const& def = defs[i];
        auto&       sc  = states[i];

        sc.prev_position_km = sc.position_km;
        stepSpacecraft(def, sc, ss, scaled_dt);

        if (debug_frame % 60 == 0)
        {
            // Find Earth index
            std::size_t earth_idx = 3;
            for (std::size_t j = 0; j < ss.defs.size(); ++j)
                if (std::string_view(ss.defs[j].name) == "Earth") { earth_idx = j; break; }

            dvec3 earth_pos = glm::mix(ss.states[earth_idx].prev_position_km,
                                       ss.states[earth_idx].position_km,
                                       ss.render_alpha);
            dvec3 rel       = sc.position_km - earth_pos;
            double dist     = glm::length(rel);
            double speed    = glm::length(sc.velocity_km - ss.states[earth_idx].velocity_km);
            double v_circ   = std::sqrt(G_km * ss.defs[earth_idx].mass_kg / dist);

            spdlog::info("SC[{}] dist_earth={:.1f} km  alt={:.1f} km  rel_speed={:.3f} km/s  v_circ={:.3f} km/s  scaled_dt={:.4f}s  render_alpha={:.4f}",
                i, dist, dist - ss.defs[earth_idx].radius_km, speed, v_circ, scaled_dt, ss.render_alpha);
        }
    }
    ++debug_frame;
}

// ---------------------------------------------------------------------------
// Spawn helpers
// ---------------------------------------------------------------------------

void spawnSpacecraftAtEarth(std::vector<SpacecraftDef>&  defs,
                            std::vector<SpacecraftState>& states,
                            SolarSystem const&            ss)
{
    // Find Earth by name (index 3 in the default solar system)
    std::size_t earth_idx = 3;
    for (std::size_t i = 0; i < ss.defs.size(); ++i)
    {
        if (std::string_view(ss.defs[i].name) == "Earth")
        {
            earth_idx = i;
            break;
        }
    }

    double const earth_radius_km = ss.defs[earth_idx].radius_km; // 6371 km
    double const altitude_km     = 400.0;
    double const r_orbit         = earth_radius_km + altitude_km;

    // Circular orbital speed around Earth (km/s)
    double const GM_earth  = G_km * ss.defs[earth_idx].mass_kg;
    double const v_circ    = std::sqrt(GM_earth / r_orbit);

    dvec3 const earth_pos = ss.states[earth_idx].position_km;
    dvec3 const earth_vel = ss.states[earth_idx].velocity_km;

    // Spawn east (+X) of Earth, orbiting toward +Z (equatorial prograde)
    SpacecraftDef def;
    SpacecraftState state;
    state.position_km      = earth_pos + dvec3(r_orbit, 0.0, 0.0);
    state.prev_position_km = state.position_km;
    state.velocity_km      = earth_vel + dvec3(0.0, 0.0, v_circ);
    // Default orientation: nose (+Y) pointing forward in orbit (+Z prograde)
    state.orientation = glm::dquat(glm::dvec3(0.0, 0.0, 0.0)); // identity (nose = +Y)

    defs.push_back(def);
    states.push_back(state);
}
