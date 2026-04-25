#include "Spacecraft.h"
#include "SolarSystem.h"
#include "Physics.h"

#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

#include <spdlog/spdlog.h>

using glm::dvec3;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr double GM_SUN = 1.32712440018e11; // km^3 / s^2
static constexpr double G_km   = 6.674e-20;        // km^3 / (kg * s^2)

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

static void addTriangle(Model& m,
                        glm::vec3 v0, glm::vec3 v1, glm::vec3 v2)
{
    glm::vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));
    glm::vec3 t = glm::normalize(v1 - v0);
    glm::vec3 b = glm::normalize(v2 - v0);
    auto base = static_cast<uint32_t>(m.vertices.size());

    m.vertices.push_back({v0, {0.0f, 0.0f}, normal, {0.0f, 0.0f}, t, b});
    m.vertices.push_back({v1, {1.0f, 0.0f}, normal, {1.0f, 0.0f}, t, b});
    m.vertices.push_back({v2, {0.5f, 1.0f}, normal, {0.5f, 1.0f}, t, b});

    m.indices.push_back(base);
    m.indices.push_back(base + 1);
    m.indices.push_back(base + 2);
}

Model createBoxMesh()
{
    // Half-extents — proportions: 5m wide × 30m tall × 5m deep.
    // Scaled at render time by visual_scale_km where 1 unit = 5m.
    constexpr float hx  = 0.5f;   // width  / 2  → 1 unit wide
    constexpr float hy  = 3.0f;   // length / 2  → 6 units tall  (+Y = forward/nose)
    constexpr float hz  = 0.5f;   // depth  / 2  → 1 unit deep
    constexpr float tip = 1.5f;   // nose pyramid height above +Y face

    // Corners of the top (nose-end) of the body box
    const glm::vec3 A{-hx, +hy, -hz};
    const glm::vec3 B{+hx, +hy, -hz};
    const glm::vec3 C{+hx, +hy, +hz};
    const glm::vec3 D{-hx, +hy, +hz};
    // Apex of the nose pyramid
    const glm::vec3 P{0.0f, +hy + tip, 0.0f};

    Model m;

    // Nose — 4 triangular pyramid faces meeting at apex P
    addTriangle(m, D, C, P); // +Z side
    addTriangle(m, C, B, P); // +X side
    addTriangle(m, B, A, P); // -Z side
    addTriangle(m, A, D, P); // -X side

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

void updateSpacecrafts(std::vector<SpacecraftDef> const& defs,
                       std::vector<SpacecraftState>&      states,
                       SolarSystem const&                 ss,
                       double                             scaled_dt)
{
    constexpr double SC_DT       = 1.0;
    constexpr double PLANET_STEP = 3600.0;

    for (std::size_t i = 0; i < defs.size(); ++i)
    {
        auto const& def = defs[i];
        auto&       sc  = states[i];

        sc.prev_position_km = sc.position_km;

        // Thrust: constant direction over each substep (orientation fixed per step).
        dvec3 fwd      = dvec3(glm::mat3_cast(glm::quat(sc.orientation)) * glm::vec3(0, 1, 0));
        dvec3 thrust_a = fwd * (sc.thrust_level * def.thrust_N / def.mass_kg * 1e-3);

        // Pre-collect body bracket positions (planet step prev/curr) and GMs.
        // These don't change between substeps — only alpha changes.
        struct BodyBracket { dvec3 prev, curr; double GM; };
        std::vector<BodyBracket> bodies;
        bodies.reserve(1 + ss.defs.size() + ss.moon_states.size());

        bodies.push_back({dvec3(0.0), dvec3(0.0), GM_SUN}); // Sun, stationary

        for (std::size_t j = 1; j < ss.defs.size(); ++j)    // planets (skip Sun at 0)
            bodies.push_back({ss.states[j].prev_position_km,
                              ss.states[j].position_km,
                              G_km * ss.defs[j].mass_kg});

        for (auto const& moon : ss.moon_states)
        {
            auto const& moon_def = ss.defs[moon.parent_planet_index].moons[moon.moon_index];
            bodies.push_back({moon.prev_position_km,
                              moon.position_km,
                              G_km * moon_def.mass_kg});
        }

        std::vector<Attractor> attractors(bodies.size());

        if (sc.thrust_level > 0.0)
        {
            // During burn: step every frame so the orbit preview updates smoothly.
            double alpha_begin = std::clamp((ss.simulation_time_s - scaled_dt) / PLANET_STEP, 0.0, 1.0);
            double alpha_end   = std::clamp( ss.simulation_time_s              / PLANET_STEP, 0.0, 1.0);
            for (std::size_t k = 0; k < bodies.size(); ++k)
                attractors[k] = {glm::mix(bodies[k].prev, bodies[k].curr, alpha_begin),
                                 glm::mix(bodies[k].prev, bodies[k].curr, alpha_end),
                                 bodies[k].GM};
            leapfrogKDK(sc.position_km, sc.velocity_km, attractors, scaled_dt, thrust_a);
        }
        else
        {
            sc.time_accumulator += scaled_dt;
            while (sc.time_accumulator >= SC_DT)
            {
                double alpha_begin = std::clamp((ss.simulation_time_s - sc.time_accumulator)         / PLANET_STEP, 0.0, 1.0);
                double alpha_end   = std::clamp((ss.simulation_time_s - sc.time_accumulator + SC_DT) / PLANET_STEP, 0.0, 1.0);

                for (std::size_t k = 0; k < bodies.size(); ++k)
                    attractors[k] = {glm::mix(bodies[k].prev, bodies[k].curr, alpha_begin),
                                     glm::mix(bodies[k].prev, bodies[k].curr, alpha_end),
                                     bodies[k].GM};

                leapfrogKDK(sc.position_km, sc.velocity_km, attractors, SC_DT, thrust_a);
                sc.time_accumulator -= SC_DT;
            }
        }
    }
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

    // Use the same interpolated position gravAccel will see, so the spacecraft
    // starts at the correct distance from Earth's gravity reference point.
    dvec3 const earth_pos = glm::mix(ss.states[earth_idx].prev_position_km,
                                     ss.states[earth_idx].position_km,
                                     ss.render_alpha);
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
