#include "SolarSystemScene.h"

#include "Object.h"
#include "Material.h"
#include "Physics.h"
#include "Pipelines/Planet.h"
#include "Spacecraft.h"
#include "Maneuver.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <numbers>
#include <numeric>
#include <cstring>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static Buffer createLineVertexBuffer(RenderingState const& state,
                                     std::vector<LineVertex> const& verts)
{
    vk::DeviceSize size = sizeof(LineVertex) * verts.size();
    auto [buf, mem] = createBuffer(state, size,
        vk::BufferUsageFlagBits::eVertexBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    void* data = mem.mapMemory(0, size).value;
    std::memcpy(data, verts.data(), static_cast<std::size_t>(size));
    mem.unmapMemory();
    return {std::move(buf), std::move(mem)};
}

static constexpr double GM_SUN_SCENE   = 1.32712440018e11;
static constexpr double GM_EARTH_SCENE = 3.986e5;
static constexpr double G_KM3          = 6.674e-20;

// Keplerian ellipse in 3D, vertices in km relative to Sun (focus). N LINE_LIST segments.
static std::pair<std::vector<LineVertex>, std::vector<uint32_t>>
makeOrbitEllipseGeometry(OsculatingOrbit const& orbit, glm::vec4 color, int N = 1024)
{
    std::vector<LineVertex> verts(N);
    double const b_frac = std::sqrt(1.0 - orbit.e * orbit.e); // b/a = sqrt(1-e^2)

    for (int i = 0; i < N; ++i)
    {
        double const E   = 2.0 * std::numbers::pi_v<double> * i / N; // eccentric anomaly
        double const x   = orbit.a * (std::cos(E) - orbit.e);         // km from Sun (focus)
        double const z   = orbit.a * b_frac * std::sin(E);            // km

        glm::dvec3 const pos = x * orbit.e_hat + z * orbit.q_hat;

        verts[i].pos   = glm::vec3(pos);  // float precision fine for display
        verts[i].color = color;
        verts[i].param = static_cast<float>(i) / static_cast<float>(N);
    }

    std::vector<uint32_t> indices;
    indices.reserve(N * 2);
    for (int i = 0; i < N; ++i)
    {
        indices.push_back(static_cast<uint32_t>(i));
        indices.push_back(static_cast<uint32_t>((i + 1) % N));
    }
    return {verts, indices};
}

// Unit circle in XZ plane, radius 1. Used for spacecraft orbit rings driven by model matrix.
static std::pair<std::vector<LineVertex>, std::vector<uint32_t>>
makeUnitCircleGeometry(glm::vec4 color, int N = 1024)
{
    std::vector<LineVertex> verts(N);
    for (int i = 0; i < N; ++i)
    {
        float const E  = 2.0f * std::numbers::pi_v<float> * i / N;
        verts[i].pos   = { std::cos(E), 0.0f, std::sin(E) };
        verts[i].color = color;
        verts[i].param = static_cast<float>(i) / static_cast<float>(N);
    }
    std::vector<uint32_t> indices;
    indices.reserve(N * 2);
    for (int i = 0; i < N; ++i)
    {
        indices.push_back(static_cast<uint32_t>(i));
        indices.push_back(static_cast<uint32_t>((i + 1) % N));
    }
    return {verts, indices};
}

// Build a 4x4 model matrix that transforms the unit circle (XZ plane, radius 1)
// into a Keplerian ellipse. The focus (e.g. Earth) is at the matrix origin.
// obj.position should be set to the focus position in camera-relative space.
static glm::mat4 orbitRingMatrix(OsculatingOrbit const& orbit)
{
    float const a_f = static_cast<float>(orbit.a);
    float const b_f = static_cast<float>(orbit.a * std::sqrt(1.0 - orbit.e * orbit.e));
    float const ae_f = static_cast<float>(orbit.a * orbit.e); // focus offset from ellipse center
    glm::vec3 const eh(orbit.e_hat);
    glm::vec3 const qh(orbit.q_hat);
    // Translate so ellipse center sits at -ae*e_hat from the focus
    // (focus is the origin; ellipse center is ae along periapsis direction)
    glm::mat4 m(
        glm::vec4(a_f * eh,                0.0f),   // col 0: X → e_hat scaled by a
        glm::vec4(0.0f, 1.0f, 0.0f,        0.0f),   // col 1: Y unchanged
        glm::vec4(b_f * qh,                0.0f),   // col 2: Z → q_hat scaled by b
        glm::vec4(-ae_f * eh,              1.0f)    // col 3: shift center to put focus at origin
    );
    return m;
}

// Grid in XZ plane: (2*half_n+1) lines in each direction, unit spacing.
static std::pair<std::vector<LineVertex>, std::vector<uint32_t>>
makeGridGeometry(glm::vec4 color, int half_n = 10)
{
    std::vector<LineVertex> verts;
    std::vector<uint32_t>   indices;
    int const total_lines = 2 * half_n + 1;
    verts.reserve(total_lines * 4);
    indices.reserve(total_lines * 4);

    auto add_line = [&](glm::vec3 a, glm::vec3 b)
    {
        auto base = static_cast<uint32_t>(verts.size());
        verts.push_back({a, color, 0.0f});
        verts.push_back({b, color, 1.0f});
        indices.push_back(base);
        indices.push_back(base + 1);
    };

    float half = static_cast<float>(half_n);
    for (int i = -half_n; i <= half_n; ++i)
    {
        float f = static_cast<float>(i);
        add_line(glm::vec3(f, 0.0f, -half), glm::vec3(f, 0.0f, half));
        add_line(glm::vec3(-half, 0.0f, f), glm::vec3(half, 0.0f, f));
    }
    return {verts, indices};
}

// Small cross marker: 4 arms in ±e_hat and ±q_hat directions, arm_km long each.
// Used to mark the burn node position on the current orbit ring.
// Returns a host-mapped-ready vertex/index buffer (4 verts, 4 indices, LINE_LIST).
static std::pair<std::vector<LineVertex>, std::vector<uint32_t>>
makeNodeCrossGeometry(float arm_km, glm::vec4 color)
{
    std::vector<LineVertex> verts = {
        { glm::vec3( arm_km, 0.0f,    0.0f),    color, 0.0f },
        { glm::vec3(-arm_km, 0.0f,    0.0f),    color, 1.0f },
        { glm::vec3(   0.0f, 0.0f,  arm_km),    color, 0.0f },
        { glm::vec3(   0.0f, 0.0f, -arm_km),    color, 1.0f },
    };
    std::vector<uint32_t> indices = {0, 1, 2, 3};
    return {verts, indices};
}

// ---------------------------------------------------------------------------
// Public functions
// ---------------------------------------------------------------------------

void initPlanetObjects(Scene& scene, SolarSystem& ss,
                       DrawableMesh const& mesh, Camera const& cam)
{
    Material const planet_material{
        .name = {"Planet"},
        .program = 2,
        .shader_data = {}
    };

    for (std::size_t i = 0; i < ss.defs.size(); ++i)
    {
        auto const& def   = ss.defs[i];
        auto&       state = ss.states[i];

        auto obj = createObject(mesh);
        obj.material  = planet_material;
        obj.position  = glm::vec3(state.position_km - cam.pos_d);
        obj.scale     = static_cast<float>(def.radius_km);
        obj.rotation  = glm::vec3(0.0f, 1.0f, 0.0f);
        obj.angel     = 0.0f;

        state.scene_object_index = static_cast<int>(scene.objs.size());
        addObject(scene, obj);
    }

}

void initMoonObjects(Scene& scene, SolarSystem& ss,
                     DrawableMesh const& mesh, Camera const& cam)
{
    Material const planet_material{
        .name = {"Planet"},
        .program = 2,
        .shader_data = {}
    };

    for (std::size_t k = 0; k < ss.moon_states.size(); ++k)
    {
        auto&       moon_state = ss.moon_states[k];
        auto const& moon_def   = ss.defs[moon_state.parent_planet_index].moons[moon_state.moon_index];

        auto obj = createObject(mesh);
        obj.material  = planet_material;
        obj.position  = glm::vec3(moon_state.position_km - cam.pos_d);
        obj.scale     = static_cast<float>(moon_def.radius_km);
        obj.rotation  = glm::vec3(0.0f, 1.0f, 0.0f);
        obj.angel     = 0.0f;

        moon_state.scene_object_index = static_cast<int>(scene.objs.size());
        addObject(scene, obj);
    }
}

SolarSystemLineObjects initOrbitLines(RenderingState const& state, Scene& scene,
                                      SolarSystem const& ss, Camera const& cam)
{
    Material const lines_material{.name = {"Lines"}, .program = 3, .shader_data = {}};

    SolarSystemLineObjects result;

    // Orbit rings — one per planet (skip Sun at index 0)
    for (std::size_t i = 0; i < ss.defs.size(); ++i)
    {
        if (ss.defs[i].semi_major_axis_km <= 0.0) continue;

        // Derive true ellipse from initial state vectors (position + velocity).
        OsculatingOrbit const orbit = computeOsculatingOrbit(
            ss.states[i].position_km, ss.states[i].velocity_km, GM_SUN_SCENE);

        glm::vec3 const col = ss.defs[i].albedo_color;
        auto [ring_verts, ring_indices] = makeOrbitEllipseGeometry(orbit, glm::vec4(col, 1.0f));

        auto vbuf = createLineVertexBuffer(state, ring_verts);
        auto ibuf = createIndexBuffer(state, ring_indices);

        Object obj{};
        obj.vertex_buffer = vbuf.buffer;
        obj.index_buffer  = ibuf.buffer;
        obj.indices_size  = static_cast<uint32_t>(ring_indices.size());
        obj.position      = glm::vec3(-cam.pos_d);
        obj.rotation      = glm::vec3(0.0f, 1.0f, 0.0f);
        obj.angel         = 0.0f;
        obj.scale         = 1.0f;  // vertices are already in km
        obj.material      = lines_material;
        obj.line_width    = ss.orbit_line_width;
        obj.line_alpha    = ss.orbit_opacity;
        obj.dash_count    = ss.orbit_stippled ? 20.0f : 0.0f;
        obj.visible       = ss.show_orbits;

        result.orbit_ring_obj_ids.push_back(static_cast<int>(scene.objs.size()));
        addObject(scene, obj);
        result.orbit_ring_vbufs.push_back(std::move(vbuf));
        result.orbit_ring_ibufs.push_back(std::move(ibuf));
    }

    // Moon orbit rings (unit circle + model matrix, like spacecraft orbit rings)
    for (std::size_t k = 0; k < ss.moon_states.size(); ++k)
    {
        auto const& moon     = ss.moon_states[k];
        std::size_t const pi = static_cast<std::size_t>(moon.parent_planet_index);
        double const parent_GM = G_KM3 * ss.defs[pi].mass_kg;

        glm::dvec3 const r_moon = moon.position_km - ss.states[pi].position_km;
        glm::dvec3 const v_moon = moon.velocity_km  - ss.states[pi].velocity_km;
        OsculatingOrbit const morb = computeOsculatingOrbit(r_moon, v_moon, parent_GM);

        glm::vec3 const pcol = ss.defs[pi].albedo_color;
        auto [mv, mi] = makeUnitCircleGeometry(glm::vec4(pcol * 0.6f + glm::vec3(0.4f), 1.0f), 512);

        auto vbuf = createLineVertexBuffer(state, mv);
        auto ibuf = createIndexBuffer(state, mi);

        Object obj{};
        obj.vertex_buffer     = vbuf.buffer;
        obj.index_buffer      = ibuf.buffer;
        obj.indices_size      = static_cast<uint32_t>(mi.size());
        obj.position          = glm::vec3(ss.states[pi].position_km - cam.pos_d);
        obj.rotation          = glm::vec3(0.0f, 1.0f, 0.0f);
        obj.angel             = 0.0f;
        obj.scale             = 1.0f;
        obj.rotation_override = orbitRingMatrix(morb);
        obj.material          = lines_material;
        obj.line_width        = ss.orbit_line_width;
        obj.line_alpha        = ss.orbit_opacity;
        obj.dash_count        = 0.0f;
        obj.visible           = ss.show_orbits;

        result.moon_orbit_obj_ids.push_back(static_cast<int>(scene.objs.size()));
        addObject(scene, obj);
        result.moon_orbit_vbufs.push_back(std::move(vbuf));
        result.moon_orbit_ibufs.push_back(std::move(ibuf));
    }

    // Ecliptic grid
    auto [grid_verts, grid_indices] = makeGridGeometry(
        glm::vec4(0.5f, 0.5f, 0.65f, 1.0f),
        ss.grid_line_count);
    auto grid_vbuf = createLineVertexBuffer(state, grid_verts);
    auto grid_ibuf = createIndexBuffer(state, grid_indices);

    result.grid_obj_id = static_cast<int>(scene.objs.size());
    {
        Object obj{};
        obj.vertex_buffer = grid_vbuf.buffer;
        obj.index_buffer  = grid_ibuf.buffer;
        obj.indices_size  = static_cast<uint32_t>(grid_indices.size());
        obj.position      = glm::vec3(cam.orbit_target - cam.pos_d);
        obj.rotation      = glm::vec3(0.0f, 1.0f, 0.0f);
        obj.angel         = 0.0f;
        obj.scale         = ss.grid_spacing_km;
        obj.material      = lines_material;
        obj.line_width    = ss.grid_line_width;
        obj.line_alpha    = ss.grid_opacity;
        obj.dash_count    = 0.0f;
        obj.visible       = ss.show_grid;
        addObject(scene, obj);
    }
    result.orbit_ring_vbufs.push_back(std::move(grid_vbuf));
    result.orbit_ring_ibufs.push_back(std::move(grid_ibuf));

    return result;
}

// ---------------------------------------------------------------------------
// Spacecraft line objects (orbit ring + predicted path)
// ---------------------------------------------------------------------------

// Forward-integrate a copy of the solar system to build a spacecraft trajectory.
// Analytical Keplerian conic path relative to the spacecraft's dominant body.
// Hyperbolic: samples from current true anomaly to SOI exit.
// Elliptic: samples one full orbit (stopping at SOI boundary if applicable).
static std::vector<glm::dvec3> buildSpacecraftPath(SolarSystem const& ss,
                                                    int sc_idx, double /*duration_s*/)
{
    auto const& sc        = ss.spacecraft_states[sc_idx];
    bool  const dom_is_moon  = sc.dominant_is_moon;
    int   const dom_moon_idx = sc.dominant_moon_idx;
    int   const dom_body_idx = sc.dominant_body_idx;

    double ref_radius_km;
    double soi_exit_km;
    double ref_GM;
    glm::dvec3 ref_pos;
    glm::dvec3 ref_vel;

    if (dom_is_moon && dom_moon_idx >= 0 &&
        dom_moon_idx < static_cast<int>(ss.moon_states.size()))
    {
        std::size_t const mi = static_cast<std::size_t>(dom_moon_idx);
        auto const& ms       = ss.moon_states[mi];
        auto const& moon_def = ss.defs[ms.parent_planet_index].moons[ms.moon_index];
        ref_radius_km = moon_def.radius_km;
        soi_exit_km   = moon_def.soi_km;
        ref_GM        = G_KM3 * moon_def.mass_kg;
        ref_pos       = interpolatedMoonPosition(ss, mi);
        ref_vel       = interpolatedMoonVelocity(ss, mi);
    }
    else if (dom_body_idx == 0)
    {
        ref_radius_km = ss.defs[0].radius_km;
        soi_exit_km   = 1e13;
        ref_GM        = G_KM3 * ss.defs[0].mass_kg;
        ref_pos       = glm::dvec3(0.0);
        ref_vel       = glm::dvec3(0.0);
    }
    else
    {
        std::size_t const bi = static_cast<std::size_t>(dom_body_idx);
        ref_radius_km = ss.defs[bi].radius_km;
        soi_exit_km   = ss.defs[bi].soi_km > 0.0 ? ss.defs[bi].soi_km : 1e13;
        ref_GM        = G_KM3 * ss.defs[bi].mass_kg;
        ref_pos       = interpolatedPosition(ss, bi);
        ref_vel       = interpolatedVelocity(ss, bi);
    }

    glm::dvec3 const r_rel = sc.position_km - ref_pos;
    glm::dvec3 const v_rel = sc.velocity_km - ref_vel;
    OsculatingOrbit const orb = computeOsculatingOrbit(r_rel, v_rel, ref_GM);

    double const p = orb.a * (1.0 - orb.e * orb.e);
    if (p <= 0.0) return {};

    double const nu0 = std::atan2(glm::dot(r_rel, orb.q_hat), glm::dot(r_rel, orb.e_hat));

    auto sampleOrbit = [&](double nu) -> glm::dvec3 {
        double const r = p / (1.0 + orb.e * std::cos(nu));
        return r * (std::cos(nu) * orb.e_hat + std::sin(nu) * orb.q_hat);
    };

    constexpr int N = 300;
    std::vector<glm::dvec3> positions;
    positions.reserve(N + 1);

    if (orb.e >= 1.0)
    {
        // Hyperbolic: nu0 → outgoing SOI exit angle (positive side of hyperbola).
        // r(nu) = p/(1+e*cos(nu)) = soi_exit => cos(nu_exit) = (p/soi_exit - 1)/e
        double const cos_nu_exit = std::clamp((p / soi_exit_km - 1.0) / orb.e, -1.0, 1.0);
        double const nu_exit     = std::acos(cos_nu_exit);
        if (nu_exit - nu0 < 1e-9) return {};

        for (int k = 0; k <= N; ++k)
        {
            double const    nu = nu0 + (nu_exit - nu0) * k / N;
            glm::dvec3 const pt = sampleOrbit(nu);
            if (glm::length(pt) < ref_radius_km) break;
            positions.push_back(pt);
        }
    }
    else
    {
        // Elliptic: one full orbit, stopping if the path exits SOI or hits the surface.
        double const nu_end = nu0 + 2.0 * M_PI;
        for (int k = 0; k <= N; ++k)
        {
            double const    nu   = nu0 + (nu_end - nu0) * k / N;
            glm::dvec3 const pt   = sampleOrbit(nu);
            double     const dist = glm::length(pt);
            if (dist < ref_radius_km) break;
            if (dist > soi_exit_km)   break;
            positions.push_back(pt);
        }
    }

    return positions;
}

void initSpacecraftLines(RenderingState const& state, Scene& scene,
                         SolarSystem const& ss, Camera const& cam,
                         SolarSystemLineObjects& line_objs)
{
    Material const lines_mat{.name = {"Lines"}, .program = 3, .shader_data = {}};
    constexpr std::size_t earth_idx = 3;

    glm::dvec3 earth_vel = interpolatedVelocity(ss, earth_idx);

    for (std::size_t i = 0; i < ss.spacecraft_states.size(); ++i)
    {
        auto const& sc = ss.spacecraft_states[i];

        // --- Orbit ring (unit circle driven by model matrix — no per-frame geometry upload) ---
        glm::dvec3 earth_pos_phys   = planetPositionAtSpacecraftTime(ss, earth_idx, i);
        glm::dvec3 earth_pos_render = interpolatedPosition(ss, earth_idx);
        glm::dvec3 r_rel = sc.position_km - earth_pos_phys;
        glm::dvec3 v_rel = sc.velocity_km  - earth_vel;
        OsculatingOrbit orbit = computeOsculatingOrbit(r_rel, v_rel, GM_EARTH_SCENE);

        auto [ring_v, ring_i] = makeUnitCircleGeometry(glm::vec4(1.0f, 0.9f, 0.3f, 1.0f));

        auto ov = createLineVertexBuffer(state, ring_v);
        auto oi = createIndexBuffer(state, ring_i);

        Object ro{};
        ro.vertex_buffer     = ov.buffer;
        ro.index_buffer      = oi.buffer;
        ro.indices_size      = static_cast<uint32_t>(ring_i.size());
        ro.position          = glm::vec3(earth_pos_render - cam.pos_d);
        ro.scale             = 1.0f;
        ro.rotation          = glm::vec3(0.0f, 1.0f, 0.0f);
        ro.rotation_override = orbitRingMatrix(orbit);
        ro.material          = lines_mat;
        ro.line_width        = ss.spacecraft_orbit_line_width;
        ro.line_alpha        = ss.spacecraft_orbit_opacity;
        ro.visible           = ss.show_spacecraft_orbit && orbit.e < 1.0;

        line_objs.sc_orbit_obj_ids.push_back(static_cast<int>(scene.objs.size()));
        addObject(scene, ro);
        line_objs.sc_orbit_vbufs.push_back(std::move(ov));
        line_objs.sc_orbit_ibufs.push_back(std::move(oi));

        // --- Predicted path (pre-allocated buffer, filled on first update) ---
        std::vector<LineVertex> empty_verts(SolarSystemLineObjects::MAX_PATH_VERTS);
        std::vector<uint32_t>   empty_idx(SolarSystemLineObjects::MAX_PATH_VERTS);
        std::iota(empty_idx.begin(), empty_idx.end(), 0u);

        auto pv = createLineVertexBuffer(state, empty_verts);
        auto pi = createIndexBuffer(state, empty_idx);

        Object po{};
        po.vertex_buffer = pv.buffer;
        po.index_buffer  = pi.buffer;
        po.indices_size  = 0; // nothing drawn until first rebuild
        po.position      = glm::vec3(earth_pos_render - cam.pos_d);
        po.scale         = 1.0f;
        po.rotation      = glm::vec3(0.0f, 1.0f, 0.0f);
        po.material      = lines_mat;
        po.line_width    = 1.5f;
        po.line_alpha    = 1.0f;
        po.visible       = false;

        line_objs.sc_path_obj_ids.push_back(static_cast<int>(scene.objs.size()));
        addObject(scene, po);
        line_objs.sc_path_vbufs.push_back(std::move(pv));
        line_objs.sc_path_ibufs.push_back(std::move(pi));

        // --- Post-burn maneuver orbit ring (unit circle, cyan, initially hidden) ---
        auto [mov, moi] = makeUnitCircleGeometry(glm::vec4(0.2f, 0.9f, 1.0f, 1.0f));
        auto mv = createLineVertexBuffer(state, mov);
        auto mi = createIndexBuffer(state, moi);

        Object mro{};
        mro.vertex_buffer     = mv.buffer;
        mro.index_buffer      = mi.buffer;
        mro.indices_size      = static_cast<uint32_t>(moi.size());
        mro.position          = glm::vec3(earth_pos_render - cam.pos_d);
        mro.scale             = 1.0f;
        mro.rotation          = glm::vec3(0.0f, 1.0f, 0.0f);
        mro.rotation_override = orbitRingMatrix(orbit);
        mro.material          = lines_mat;
        mro.line_width        = 1.5f;
        mro.line_alpha        = 0.8f;
        mro.visible           = false;

        line_objs.sc_maneuver_orbit_obj_ids.push_back(static_cast<int>(scene.objs.size()));
        addObject(scene, mro);
        line_objs.sc_maneuver_orbit_vbufs.push_back(std::move(mv));
        line_objs.sc_maneuver_orbit_ibufs.push_back(std::move(mi));

        // --- Encounter hyperbolic arc (pre-allocated, initially hidden) ---
        std::vector<LineVertex> enc_empty(SolarSystemLineObjects::MAX_ENCOUNTER_VERTS);
        std::vector<uint32_t>   enc_idx(SolarSystemLineObjects::MAX_ENCOUNTER_VERTS);
        std::iota(enc_idx.begin(), enc_idx.end(), 0u);

        auto ev = createLineVertexBuffer(state, enc_empty);
        auto ei = createIndexBuffer(state, enc_idx);

        Object eo{};
        eo.vertex_buffer = ev.buffer;
        eo.index_buffer  = ei.buffer;
        eo.indices_size  = 0;
        eo.position      = glm::vec3(0.0f);
        eo.scale         = 1.0f;
        eo.rotation      = glm::vec3(0.0f, 1.0f, 0.0f);
        eo.material      = lines_mat;
        eo.line_width    = 2.0f;
        eo.line_alpha    = 1.0f;
        eo.visible       = false;

        line_objs.sc_encounter_path_obj_ids.push_back(static_cast<int>(scene.objs.size()));
        addObject(scene, eo);
        line_objs.sc_encounter_path_vbufs.push_back(std::move(ev));
        line_objs.sc_encounter_path_ibufs.push_back(std::move(ei));

        // --- Heliocentric transfer orbit arc (pre-allocated, initially hidden) ---
        std::vector<LineVertex> helio_empty(SolarSystemLineObjects::MAX_HELIO_ORBIT_VERTS);
        std::vector<uint32_t>   helio_idx(SolarSystemLineObjects::MAX_HELIO_ORBIT_VERTS);
        std::iota(helio_idx.begin(), helio_idx.end(), 0u);

        auto hv = createLineVertexBuffer(state, helio_empty);
        auto hi = createIndexBuffer(state, helio_idx);

        Object ho{};
        ho.vertex_buffer = hv.buffer;
        ho.index_buffer  = hi.buffer;
        ho.indices_size  = 0;
        ho.position      = glm::vec3(0.0f);
        ho.scale         = 1.0f;
        ho.rotation      = glm::vec3(0.0f, 1.0f, 0.0f);
        ho.material      = lines_mat;
        ho.line_width    = 2.0f;
        ho.line_alpha    = 1.0f;
        ho.visible       = false;

        line_objs.sc_helio_orbit_obj_ids.push_back(static_cast<int>(scene.objs.size()));
        addObject(scene, ho);
        line_objs.sc_helio_orbit_vbufs.push_back(std::move(hv));
        line_objs.sc_helio_orbit_ibufs.push_back(std::move(hi));
    }

    // --- Burn node cross marker (one shared, hidden until planning) ---
    {
        auto [cv, ci] = makeNodeCrossGeometry(80.0f, glm::vec4(1.0f, 0.6f, 0.0f, 1.0f));
        auto cnv = createLineVertexBuffer(state, cv);
        auto cni = createIndexBuffer(state, ci);

        Object cno{};
        cno.vertex_buffer = cnv.buffer;
        cno.index_buffer  = cni.buffer;
        cno.indices_size  = static_cast<uint32_t>(ci.size());
        cno.position      = glm::vec3(0.0f);
        cno.scale         = 1.0f;
        cno.rotation      = glm::vec3(0.0f, 1.0f, 0.0f);
        cno.material      = lines_mat;
        cno.line_width    = 2.0f;
        cno.line_alpha    = 1.0f;
        cno.visible       = false;

        line_objs.maneuver_node_obj_id = static_cast<int>(scene.objs.size());
        addObject(scene, cno);
        line_objs.maneuver_node_vbuf = std::move(cnv);
        line_objs.maneuver_node_ibuf = std::move(cni);
    }
}

void writePlanetMaterialBuffers(Scene& scene, SolarSystem const& ss, int frame)
{
    // Count objects in programs 0 and 1 to find the base index for program 2.
    int base_index = 0;
    for (auto const& [prog, obj_list] : scene.programs)
    {
        if (prog >= 2) break;
        base_index += static_cast<int>(obj_list.size());
    }

    int const prog2_count = static_cast<int>(scene.programs[2].size());
    for (int i = 0; i < prog2_count && i < static_cast<int>(ss.defs.size()); ++i)
    {
        auto const& def = ss.defs[i];

        PlanetMaterialData mat;
        mat.diffuse_texture        = def.diffuse_texture_index;
        mat.normal_texture         = def.normal_texture_index;
        mat.has_normal_map         = (def.normal_texture_index >= 0) ? 1 : 0;
        mat.has_atmosphere         = def.has_atmosphere ? 1 : 0;
        mat.atmosphere_color_scale = glm::vec4(def.atmosphere_color, def.atmosphere_scale);
        mat.albedo_color           = glm::vec4(def.albedo_color, 1.0f);
        mat.roughness              = def.roughness;
        mat.metallic               = def.metallic;
        mat.emissive               = def.emissive;
        mat.cloud_texture          = def.cloud_texture_index;

        writeBuffer(*scene.planet_material_buffer[frame], mat, base_index + i);
    }
}

void writeMoonMaterialBuffers(Scene& scene, SolarSystem const& ss, int frame)
{
    int base_index = 0;
    for (auto const& [prog, obj_list] : scene.programs)
    {
        if (prog >= 2) break;
        base_index += static_cast<int>(obj_list.size());
    }

    int const planet_count = static_cast<int>(ss.defs.size());

    for (int k = 0; k < static_cast<int>(ss.moon_states.size()); ++k)
    {
        auto const& moon_state = ss.moon_states[k];
        auto const& moon_def   = ss.defs[moon_state.parent_planet_index].moons[moon_state.moon_index];

        PlanetMaterialData mat;
        mat.diffuse_texture        = moon_def.diffuse_texture_index;
        mat.normal_texture         = moon_def.normal_texture_index;
        mat.has_normal_map         = (moon_def.normal_texture_index >= 0) ? 1 : 0;
        mat.has_atmosphere         = 0;
        mat.atmosphere_color_scale = glm::vec4(0.0f);
        mat.albedo_color           = glm::vec4(moon_def.albedo_color, 1.0f);
        mat.roughness              = moon_def.roughness;
        mat.metallic               = moon_def.metallic;
        mat.emissive               = moon_def.emissive;
        mat.cloud_texture          = -1;

        writeBuffer(*scene.planet_material_buffer[frame], mat, base_index + planet_count + k);
    }
}

void writeAtmosphereColorBuffers(Scene& scene, SolarSystem const& ss, int frame)
{
    int idx = 0;
    for (auto const& def : ss.defs)
    {
        if (!def.has_atmosphere) { ++idx; continue; }
        glm::vec4 const data{def.atmosphere_color, def.atmosphere_scale * 2.0f};
        writeBuffer(*scene.atmosphere_color_buffer[frame], data, idx);
        ++idx;
    }
}

void updateSceneFromSolarSystem(Scene& scene, SolarSystem const& ss,
                                SolarSystemLineObjects& line_objs)
{
    // Planet CRR positions and rotation angles — interpolated between the two
    // most recent physics steps for smooth sub-step rendering.
    for (std::size_t i = 0; i < ss.states.size(); ++i)
    {
        auto const& state = ss.states[i];
        if (state.scene_object_index < 0) continue;
        auto& obj = scene.objs[state.scene_object_index];

        double const interp_rot = std::lerp(state.prev_rotation_angle,
                                            state.rotation_angle, ss.render_alpha);

        obj.position = glm::vec3(interpolatedPosition(ss, i) - scene.camera.pos_d);
        obj.angel    = static_cast<float>(glm::degrees(interp_rot));
    }

    // Orbit ring objects
    int ring_idx = 0;
    for (std::size_t i = 0; i < ss.defs.size(); ++i)
    {
        if (ss.defs[i].semi_major_axis_km <= 0.0) continue;
        auto& obj      = scene.objs[line_objs.orbit_ring_obj_ids[ring_idx++]];
        obj.position   = glm::vec3(-scene.camera.pos_d);
        obj.line_width = ss.orbit_line_width;
        obj.line_alpha = ss.orbit_opacity;
        obj.dash_count = ss.orbit_stippled ? 20.0f : 0.0f;
        obj.visible    = ss.show_orbits;
    }

    // Moon orbit rings — recompute osculating orbit each frame so precession is visible
    for (std::size_t k = 0; k < ss.moon_states.size() &&
                            k < line_objs.moon_orbit_obj_ids.size(); ++k)
    {
        auto const& moon     = ss.moon_states[k];
        std::size_t const pi = static_cast<std::size_t>(moon.parent_planet_index);
        double const parent_GM = G_KM3 * ss.defs[pi].mass_kg;

        glm::dvec3 const r_moon = interpolatedMoonPosition(ss, k) - interpolatedPosition(ss, pi);
        glm::dvec3 const v_moon = interpolatedMoonVelocity(ss, k) - interpolatedVelocity(ss, pi);
        OsculatingOrbit const morb = computeOsculatingOrbit(r_moon, v_moon, parent_GM);

        auto& obj             = scene.objs[line_objs.moon_orbit_obj_ids[k]];
        obj.position          = glm::vec3(interpolatedPosition(ss, pi) - scene.camera.pos_d);
        obj.rotation_override = orbitRingMatrix(morb);
        obj.line_width        = ss.orbit_line_width;
        obj.line_alpha        = ss.orbit_opacity;
        obj.visible           = ss.show_orbits;
    }

    // Ecliptic grid
    {
        auto& obj      = scene.objs[line_objs.grid_obj_id];
        obj.position   = glm::vec3(scene.camera.orbit_target - scene.camera.pos_d);
        obj.scale      = ss.grid_spacing_km;
        obj.line_width = ss.grid_line_width;
        obj.line_alpha = ss.grid_opacity;
        obj.visible    = ss.show_grid;
    }

    // Moon positions and rotation
    for (std::size_t k = 0; k < ss.moon_states.size(); ++k)
    {
        auto const& moon = ss.moon_states[k];
        if (moon.scene_object_index < 0) continue;
        auto& obj = scene.objs[moon.scene_object_index];

        double const interp_rot = std::lerp(moon.prev_rotation_angle,
                                             moon.rotation_angle, ss.render_alpha);
        obj.position = glm::vec3(interpolatedMoonPosition(ss, k) - scene.camera.pos_d);
        obj.angel    = static_cast<float>(glm::degrees(interp_rot));
    }

    // Spacecraft positions and orientations
    for (std::size_t i = 0; i < ss.spacecraft_states.size(); ++i)
    {
        auto const& sc = ss.spacecraft_states[i];
        if (sc.scene_object_index < 0) continue;
        auto& obj = scene.objs[sc.scene_object_index];

        obj.position         = glm::vec3(interpolatedSpacecraftPosition(ss, i) - scene.camera.pos_d);
        obj.rotation_override = glm::mat4_cast(glm::quat(sc.orientation));
        obj.scale            = static_cast<float>(ss.spacecraft_defs[i].visual_scale_km);
    }

    // Spacecraft orbit rings and predicted paths
    if (line_objs.sc_orbit_obj_ids.empty()) return;

    for (std::size_t i = 0; i < ss.spacecraft_states.size(); ++i)
    {
        if (i >= line_objs.sc_orbit_obj_ids.size()) break;
        auto const& sc = ss.spacecraft_states[i];

        // --- Dominant-body reference for the osculating orbit ring ---
        glm::dvec3 ref_pos_phys;
        glm::dvec3 ref_pos_render;
        glm::dvec3 ref_vel;
        double     ref_GM;
        double     ref_radius_km;
        double     soi_exit_km;

        if (sc.dominant_is_moon && sc.dominant_moon_idx >= 0 &&
            sc.dominant_moon_idx < static_cast<int>(ss.moon_states.size()))
        {
            std::size_t const mk = static_cast<std::size_t>(sc.dominant_moon_idx);
            auto const& ms       = ss.moon_states[mk];
            auto const& moon_def = ss.defs[ms.parent_planet_index].moons[ms.moon_index];
            ref_pos_phys   = moonPositionAtSpacecraftTime(ss, mk, i);
            ref_pos_render = interpolatedMoonPosition(ss, mk);
            ref_vel        = interpolatedMoonVelocity(ss, mk);
            ref_GM         = G_KM3 * moon_def.mass_kg;
            ref_radius_km  = moon_def.radius_km;
            soi_exit_km    = moon_def.soi_km;
        }
        else if (sc.dominant_body_idx == 0)
        {
            ref_pos_phys   = glm::dvec3(0.0);
            ref_pos_render = glm::dvec3(0.0);
            ref_vel        = glm::dvec3(0.0);
            ref_GM         = GM_SUN_SCENE;
            ref_radius_km  = ss.defs[0].radius_km;
            soi_exit_km    = 1e13;
        }
        else
        {
            std::size_t const bi = static_cast<std::size_t>(sc.dominant_body_idx);
            ref_pos_phys   = planetPositionAtSpacecraftTime(ss, bi, i);
            ref_pos_render = interpolatedPosition(ss, bi);
            ref_vel        = interpolatedVelocity(ss, bi);
            ref_GM         = G_KM3 * ss.defs[bi].mass_kg;
            ref_radius_km  = ss.defs[bi].radius_km;
            soi_exit_km    = ss.defs[bi].soi_km > 0.0 ? ss.defs[bi].soi_km : 1e13;
        }

        glm::vec3  const ref_crr = glm::vec3(ref_pos_render - scene.camera.pos_d);
        // Render-interpolated relative position: keeps orbit ring smooth between
        // the 1s spacecraft physics steps instead of jumping each step.
        glm::dvec3 const sc_render_pos = interpolatedSpacecraftPosition(ss, i);
        glm::dvec3 const r_rel         = sc_render_pos - ref_pos_render;
        // Physics-time relative position: used for maneuver propagation only.
        glm::dvec3 const r_phys        = sc.position_km - ref_pos_phys;
        // Extrapolate velocity forward by the same fractional step used for r_rel so that
        // (r_rel, v_render) form a physically consistent pair at render time.  Without this,
        // r changes every frame while v stays fixed for a full 1 s step, making the computed
        // orbital energy oscillate at frame rate → the ring jitters when zoomed in close.
        glm::dvec3 v_render = sc.velocity_km;
        double const r_phys_mag = glm::length(r_phys);
        if (r_phys_mag > 1e-6) {
            glm::dvec3 const acc = -(ref_GM / (r_phys_mag * r_phys_mag * r_phys_mag)) * r_phys;
            v_render += acc * sc.time_accumulator;
        }
        glm::dvec3 const v_rel         = v_render - ref_vel;
        OsculatingOrbit orbit = computeOsculatingOrbit(r_rel, v_rel, ref_GM);

        auto& ring_obj           = scene.objs[line_objs.sc_orbit_obj_ids[i]];
        ring_obj.position        = ref_crr;
        ring_obj.rotation_override = orbitRingMatrix(orbit);
        ring_obj.line_width      = ss.spacecraft_orbit_line_width;
        ring_obj.line_alpha      = ss.spacecraft_orbit_opacity;
        ring_obj.visible         = ss.show_spacecraft_orbit && orbit.e < 1.0 && orbit.a > 0.0;

        bool const is_maneuver_sc = ss.maneuver_mode &&
                                    ss.maneuver_sc_idx == static_cast<int>(i);

        // Lambda: compute and upload the encounter arc given burn-point state.
        // dt_burn = seconds from current sim state to the burn epoch (0 for live orbit).
        // Returns true if an arc was found and uploaded; hides the object if false.
        auto drawEncounterArc = [&](glm::dvec3 const& br,
                                    OsculatingOrbit const& post_orbit,
                                    double dv_mag,
                                    double dt_burn) -> bool
        {
            if (!(i < line_objs.sc_encounter_path_obj_ids.size())) return false;
            auto& enc_obj_early = scene.objs[line_objs.sc_encounter_path_obj_ids[i]];
            if (i < line_objs.sc_helio_orbit_obj_ids.size())
                scene.objs[line_objs.sc_helio_orbit_obj_ids[i]].visible = false;
            if (dv_mag < 1e-9) { enc_obj_early.visible = false; return false; }

            // Helper: upload vertices and make the encounter arc object visible.
            auto uploadArc = [&](std::vector<LineVertex> const& verts,
                                 glm::vec3 const& body_crr) -> bool
            {
                if (verts.size() < 2) { enc_obj_early.visible = false; return false; }
                auto& evbuf = line_objs.sc_encounter_path_vbufs[i];
                vk::DeviceSize const vsz = sizeof(LineVertex) * verts.size();
                void* vptr = evbuf.memory.mapMemory(0, vsz).value;
                std::memcpy(vptr, verts.data(), static_cast<std::size_t>(vsz));
                evbuf.memory.unmapMemory();
                enc_obj_early.indices_size = static_cast<uint32_t>(verts.size());
                enc_obj_early.position     = body_crr;
                enc_obj_early.visible      = true;
                return true;
            };

            // ----------------------------------------------------------------
            // Case 1: Orbiting a moon — show trajectory in parent's frame
            //         after hyperbolic SOI exit.
            // ----------------------------------------------------------------
            if (sc.dominant_is_moon && sc.dominant_moon_idx >= 0)
            {
                if (!(post_orbit.e >= 1.0 && post_orbit.a < 0.0))
                    { enc_obj_early.visible = false; return false; }

                std::size_t const mk     = static_cast<std::size_t>(sc.dominant_moon_idx);
                auto const& ms           = ss.moon_states[mk];
                std::size_t const par_idx = static_cast<std::size_t>(ms.parent_planet_index);
                auto const& par_def      = ss.defs[par_idx];
                auto const& moon_def_hyp = par_def.moons[static_cast<std::size_t>(ms.moon_index)];
                double const GM_par      = G_KM3 * par_def.mass_kg;
                double const moon_soi    = moon_def_hyp.soi_km;

                double const e = post_orbit.e;
                double const a = post_orbit.a;   // negative
                double const p = a * (1.0 - e * e); // positive: |a|(e²-1)

                // True anomaly at SOI exit: r(nu) = soi
                double const cos_nu_exit = (p / moon_soi - 1.0) / e;
                if (cos_nu_exit >= 1.0) { enc_obj_early.visible = false; return false; }
                double const nu_exit = std::acos(std::clamp(cos_nu_exit, -1.0, 1.0));

                // Position and velocity at exit, in Moon's frame
                glm::dvec3 const r_exit =
                    moon_soi * (std::cos(nu_exit) * post_orbit.e_hat +
                                std::sin(nu_exit) * post_orbit.q_hat);
                double const sqrt_gm_p  = std::sqrt(ref_GM / p);
                double const vr_exit    = sqrt_gm_p * e * std::sin(nu_exit);
                double const vt_exit    = sqrt_gm_p * (1.0 + e * std::cos(nu_exit));
                glm::dvec3 const r_exit_hat = glm::normalize(r_exit);
                glm::dvec3 const t_exit_hat =
                    glm::normalize(glm::cross(post_orbit.h_hat, r_exit));
                glm::dvec3 const v_exit = vr_exit * r_exit_hat + vt_exit * t_exit_hat;

                // Hyperbolic TOF: M = e*sinh(F) - F, F = 2*arctanh(k*tan(nu/2))
                double const k = std::sqrt((e - 1.0) / (e + 1.0));
                auto hypM = [&](double nu) {
                    double const F = 2.0 * std::atanh(k * std::tan(nu / 2.0));
                    return e * std::sinh(F) - F;
                };
                double const n_hyp   = std::sqrt(ref_GM / ((-a) * (-a) * (-a)));
                double const nu_burn = std::atan2(glm::dot(post_orbit.q_hat, br),
                                                   glm::dot(post_orbit.e_hat, br));
                double const tof = (hypM(nu_exit) - hypM(nu_burn)) / n_hyp;
                if (tof <= 0.0) { enc_obj_early.visible = false; return false; }

                // Moon's orbit relative to parent at physics time, advanced to burn + exit
                glm::dvec3 const par_phys   = planetPositionAtSpacecraftTime(ss, par_idx, i);
                glm::dvec3 const moon_r0    = ref_pos_phys - par_phys;
                glm::dvec3 const moon_v0    = ref_vel - interpolatedVelocity(ss, par_idx);
                auto [moon_r_burn, moon_v_burn] =
                    keplerPropagate(moon_r0, moon_v0, GM_par, dt_burn);
                auto [moon_r_exit, moon_v_exit] =
                    keplerPropagate(moon_r_burn, moon_v_burn, GM_par, tof);

                // Spacecraft state in parent's frame at Moon SOI exit
                glm::dvec3 const r_sc_par = r_exit + moon_r_exit;
                glm::dvec3 const v_sc_par = v_exit + moon_v_exit;

                OsculatingOrbit const par_orbit =
                    computeOsculatingOrbit(r_sc_par, v_sc_par, GM_par);
                double const p_par = par_orbit.a * (1.0 - par_orbit.e * par_orbit.e);
                if (p_par <= 0.0) { enc_obj_early.visible = false; return false; }

                double const par_soi =
                    par_def.soi_km > 0.0 ? par_def.soi_km : 1e15;
                double const nu0_par =
                    std::atan2(glm::dot(r_sc_par, par_orbit.q_hat),
                               glm::dot(r_sc_par, par_orbit.e_hat));

                // Arc span: full orbit (elliptic) or to parent SOI exit (hyperbolic)
                double nu_span;
                if (par_orbit.e >= 1.0)
                {
                    double const cos_ne = std::clamp(
                        (p_par / par_soi - 1.0) / par_orbit.e, -1.0, 1.0);
                    nu_span = std::acos(cos_ne) - nu0_par;
                }
                else
                {
                    nu_span = 2.0 * std::numbers::pi_v<double>;
                }
                if (nu_span <= 0.0) { enc_obj_early.visible = false; return false; }

                constexpr int N_ARC = SolarSystemLineObjects::MAX_ENCOUNTER_VERTS / 2;
                std::vector<glm::dvec3> arc_pts;
                arc_pts.reserve(N_ARC + 1);

                for (int v = 0; v <= N_ARC; ++v)
                {
                    double const nu = nu0_par + nu_span * v / N_ARC;
                    double const r_mag = p_par / (1.0 + par_orbit.e * std::cos(nu));
                    if (r_mag > par_soi || r_mag < par_def.radius_km) break;
                    arc_pts.push_back(
                        r_mag * (std::cos(nu) * par_orbit.e_hat +
                                 std::sin(nu) * par_orbit.q_hat));
                }

                std::vector<LineVertex> enc_verts;
                enc_verts.reserve(arc_pts.size() * 2);
                for (std::size_t v = 0; v + 1 < arc_pts.size(); ++v)
                {
                    float const t = static_cast<float>(v) /
                        static_cast<float>(std::max(arc_pts.size() - 1, std::size_t{1}));
                    enc_verts.push_back({glm::vec3(arc_pts[v]),     {1.0f, 0.55f - 0.15f * t, 0.0f, 1.0f}, 0.0f});
                    enc_verts.push_back({glm::vec3(arc_pts[v + 1]), {1.0f, 0.55f - 0.15f * t, 0.0f, 1.0f}, 0.0f});
                }

                glm::vec3 const par_crr =
                    glm::vec3(interpolatedPosition(ss, par_idx) - scene.camera.pos_d);
                return uploadArc(enc_verts, par_crr);
            }

            // ----------------------------------------------------------------
            // Case 2: In Sun's SOI — planet encounter arcs not yet implemented.
            // ----------------------------------------------------------------
            if (sc.dominant_body_idx <= 0)
                { enc_obj_early.visible = false; return false; }

            // ----------------------------------------------------------------
            // Case 3: Orbiting a planet.
            // Sub-case 3b: hyperbolic escape — show heliocentric transfer orbit
            //              and scan for planet SOI encounters along it.
            // ----------------------------------------------------------------
            if (!sc.dominant_is_moon && sc.dominant_body_idx > 0 &&
                post_orbit.e >= 1.0 && post_orbit.a < 0.0)
            {
                if (!(i < line_objs.sc_helio_orbit_obj_ids.size()))
                    { enc_obj_early.visible = false; return false; }
                auto& helio_obj = scene.objs[line_objs.sc_helio_orbit_obj_ids[i]];

                // 3b.1 — SOI exit geometry (same math as Case 1)
                double const e = post_orbit.e;
                double const a = post_orbit.a;           // negative
                double const p = a * (1.0 - e * e);     // positive: |a|(e²-1)

                double const cos_nu_exit = (p / soi_exit_km - 1.0) / e;
                if (cos_nu_exit >= 1.0)
                    { enc_obj_early.visible = false; return false; }
                double const nu_exit =
                    std::acos(std::clamp(cos_nu_exit, -1.0, 1.0));

                glm::dvec3 const r_exit =
                    soi_exit_km * (std::cos(nu_exit) * post_orbit.e_hat +
                                   std::sin(nu_exit) * post_orbit.q_hat);
                double const sqrt_gm_p = std::sqrt(ref_GM / p);
                double const vr_exit   = sqrt_gm_p * e * std::sin(nu_exit);
                double const vt_exit   = sqrt_gm_p * (1.0 + e * std::cos(nu_exit));
                glm::dvec3 const r_exit_hat = glm::normalize(r_exit);
                glm::dvec3 const t_exit_hat =
                    glm::normalize(glm::cross(post_orbit.h_hat, r_exit));
                glm::dvec3 const v_exit =
                    vr_exit * r_exit_hat + vt_exit * t_exit_hat;

                // 3b.2 — Hyperbolic TOF from burn to SOI exit (same as Case 1)
                double const k_esc = std::sqrt((e - 1.0) / (e + 1.0));
                auto hypM_esc = [&](double nu) {
                    double const F = 2.0 * std::atanh(k_esc * std::tan(nu / 2.0));
                    return e * std::sinh(F) - F;
                };
                double const n_hyp_esc =
                    std::sqrt(ref_GM / ((-a) * (-a) * (-a)));
                double const nu_burn_esc =
                    std::atan2(glm::dot(post_orbit.q_hat, br),
                               glm::dot(post_orbit.e_hat, br));
                double const tof_esc =
                    (hypM_esc(nu_exit) - hypM_esc(nu_burn_esc)) / n_hyp_esc;
                if (tof_esc <= 0.0)
                    { enc_obj_early.visible = false; return false; }

                // 3b.3 — Propagate planet heliocentric state to burn epoch then exit epoch
                auto [planet_r_burn, planet_v_burn] =
                    keplerPropagate(ref_pos_phys, ref_vel, GM_SUN_SCENE, dt_burn);
                auto [planet_r_exit, planet_v_exit] =
                    keplerPropagate(planet_r_burn, planet_v_burn, GM_SUN_SCENE, tof_esc);

                // 3b.4 — Heliocentric state at SOI exit
                glm::dvec3 const r_sc_sun = r_exit + planet_r_exit;
                glm::dvec3 const v_sc_sun = v_exit + planet_v_exit;

                OsculatingOrbit const helio =
                    computeOsculatingOrbit(r_sc_sun, v_sc_sun, GM_SUN_SCENE);
                double const p_helio = helio.a * (1.0 - helio.e * helio.e);
                if (p_helio <= 0.0)
                    { enc_obj_early.visible = false; return false; }

                // 3b.5 — Sample and upload heliocentric arc (light blue, Sun-centred)
                {
                    double const nu0_h =
                        std::atan2(glm::dot(r_sc_sun, helio.q_hat),
                                   glm::dot(r_sc_sun, helio.e_hat));

                    double nu_span_h;
                    if (helio.e >= 1.0)
                    {
                        double const nu_max_h =
                            std::acos(std::clamp(-1.0 / helio.e, -1.0, 1.0)) - 1e-4;
                        nu_span_h = nu_max_h - nu0_h;
                    }
                    else
                    {
                        nu_span_h = 2.0 * std::numbers::pi_v<double>;
                    }

                    if (nu_span_h > 0.0)
                    {
                        constexpr int N_H =
                            SolarSystemLineObjects::MAX_HELIO_ORBIT_VERTS / 2;
                        std::vector<glm::dvec3> helio_pts;
                        helio_pts.reserve(N_H + 1);

                        for (int v = 0; v <= N_H; ++v)
                        {
                            double const nu  = nu0_h + nu_span_h * v / N_H;
                            double const r_h =
                                p_helio / (1.0 + helio.e * std::cos(nu));
                            if (r_h <= 0.0) break;
                            helio_pts.push_back(
                                r_h * (std::cos(nu) * helio.e_hat +
                                       std::sin(nu) * helio.q_hat));
                        }

                        std::vector<LineVertex> hverts;
                        hverts.reserve(helio_pts.size() * 2);
                        for (std::size_t v = 0; v + 1 < helio_pts.size(); ++v)
                        {
                            float const t = static_cast<float>(v) /
                                static_cast<float>(std::max(helio_pts.size() - 1, std::size_t{1}));
                            hverts.push_back({glm::vec3(helio_pts[v]),
                                              {0.3f, 0.85f, 1.0f, 1.0f - 0.7f * t}, 0.0f});
                            hverts.push_back({glm::vec3(helio_pts[v + 1]),
                                              {0.3f, 0.85f, 1.0f, 1.0f - 0.7f * t}, 0.0f});
                        }

                        if (hverts.size() >= 2)
                        {
                            auto& hvbuf = line_objs.sc_helio_orbit_vbufs[i];
                            vk::DeviceSize const hvsz =
                                sizeof(LineVertex) * hverts.size();
                            void* hvptr =
                                hvbuf.memory.mapMemory(0, hvsz).value;
                            std::memcpy(hvptr, hverts.data(),
                                        static_cast<std::size_t>(hvsz));
                            hvbuf.memory.unmapMemory();
                            helio_obj.indices_size =
                                static_cast<uint32_t>(hverts.size());
                            helio_obj.position =
                                glm::vec3(-scene.camera.pos_d);
                            helio_obj.visible = true;
                        }
                    }
                }

                // 3b.6 — Planet encounter scan along the heliocentric orbit
                // Same algorithm as Case 3's moon scan, one level up.
                {
                    int const dom_idx = sc.dominant_body_idx;

                    double const a_h  = helio.a;
                    double const e_h  = helio.e;
                    double const p_h  = p_helio;
                    double const n_h  = (e_h < 1.0)
                        ? std::sqrt(GM_SUN_SCENE / (a_h * a_h * a_h))
                        : 0.0;

                    double const nu0_h =
                        std::atan2(glm::dot(r_sc_sun, helio.q_hat),
                                   glm::dot(r_sc_sun, helio.e_hat));

                    auto toEccentricH = [&](double nu) {
                        return 2.0 * std::atan(
                            std::sqrt((1.0 - e_h) / (1.0 + e_h)) *
                            std::tan(nu / 2.0));
                    };

                    double M_exit_h = 0.0;
                    if (e_h < 1.0)
                    {
                        double const E0 = toEccentricH(nu0_h);
                        M_exit_h = E0 - e_h * std::sin(E0);
                    }

                    // nu_max_scan for hyperbolic helio orbit
                    double const nu_max_scan_h = (e_h >= 1.0)
                        ? std::acos(std::clamp(-1.0 / e_h, -1.0, 1.0)) - 0.01
                        : 0.0;

                    double const k_h = (e_h >= 1.0)
                        ? std::sqrt((e_h - 1.0) / (e_h + 1.0))
                        : 0.0;
                    double const n_hyp_h = (e_h >= 1.0)
                        ? std::sqrt(GM_SUN_SCENE / ((-a_h) * (-a_h) * (-a_h)))
                        : 0.0;
                    double const M0_h = (e_h >= 1.0)
                        ? [&]{ double const F = 2.0 * std::atanh(
                                   k_h * std::tan(nu0_h / 2.0));
                               return e_h * std::sinh(F) - F; }()
                        : 0.0;

                    bool found = false;

                    for (std::size_t pi = 1;
                         pi < ss.defs.size() && !found; ++pi)
                    {
                        if (static_cast<int>(pi) == dom_idx) continue;
                        auto const& tgt_def = ss.defs[pi];
                        if (tgt_def.soi_km <= 0.0) continue;

                        double const GM_tgt     = G_KM3 * tgt_def.mass_kg;
                        double const target_soi = tgt_def.soi_km;

                        glm::dvec3 const tgt_pos0 =
                            planetPositionAtSpacecraftTime(ss, pi, i);
                        glm::dvec3 const tgt_vel0 =
                            interpolatedVelocity(ss, pi);

                        // Advance target to helio orbit start epoch
                        // (dt_burn from physics time + tof_esc to planet SOI exit)
                        auto [tgt_r_t0, tgt_v_t0] =
                            keplerPropagate(tgt_pos0, tgt_vel0,
                                            GM_SUN_SCENE, dt_burn + tof_esc);

                        constexpr int N_SCAN_H = 360;
                        int entry_idx = -1;

                        for (int s = 0; s < N_SCAN_H; ++s)
                        {
                            double nu_s, tof_s;
                            if (e_h < 1.0)
                            {
                                double const nu   =
                                    nu0_h + (2.0 * M_PI * s) / N_SCAN_H;
                                double const nu_w =
                                    std::fmod(nu + 10.0 * M_PI, 2.0 * M_PI)
                                    - M_PI;
                                double const E_s  = toEccentricH(nu_w);
                                double       M_s  = E_s - e_h * std::sin(E_s);
                                if (M_s < M_exit_h) M_s += 2.0 * M_PI;
                                tof_s = (M_s - M_exit_h) / n_h;
                                nu_s  = nu_w;
                            }
                            else
                            {
                                double const nu =
                                    nu0_h + (nu_max_scan_h - nu0_h) *
                                    s / N_SCAN_H;
                                double const F_s =
                                    2.0 * std::atanh(k_h * std::tan(nu / 2.0));
                                double const M_s =
                                    e_h * std::sinh(F_s) - F_s;
                                tof_s = (M_s - M0_h) / n_hyp_h;
                                nu_s  = nu;
                            }

                            double const r_s_mag =
                                p_h / (1.0 + e_h * std::cos(nu_s));
                            if (r_s_mag <= 0.0) continue;
                            glm::dvec3 const r_s =
                                r_s_mag * (std::cos(nu_s) * helio.e_hat +
                                           std::sin(nu_s) * helio.q_hat);

                            auto [tgt_r_s, tgt_v_s] =
                                keplerPropagate(tgt_r_t0, tgt_v_t0,
                                                GM_SUN_SCENE, tof_s);

                            if (glm::length(r_s - tgt_r_s) < target_soi)
                            {
                                entry_idx = s;
                                break;
                            }
                        }

                        if (entry_idx < 0) continue;

                        // Bisect 24 iterations to refine encounter epoch
                        double nu_lo_h, nu_hi_h;
                        if (e_h < 1.0)
                        {
                            nu_lo_h = nu0_h + (2.0 * M_PI *
                                std::max(entry_idx - 1, 0)) / N_SCAN_H;
                            nu_hi_h = nu0_h + (2.0 * M_PI *
                                entry_idx) / N_SCAN_H;
                        }
                        else
                        {
                            nu_lo_h = nu0_h + (nu_max_scan_h - nu0_h) *
                                std::max(entry_idx - 1, 0) / N_SCAN_H;
                            nu_hi_h = nu0_h + (nu_max_scan_h - nu0_h) *
                                entry_idx / N_SCAN_H;
                        }

                        for (int b = 0; b < 24; ++b)
                        {
                            double const nu_mid = 0.5 * (nu_lo_h + nu_hi_h);
                            double tof_m;
                            double nu_m;

                            if (e_h < 1.0)
                            {
                                double const nu_w_m =
                                    std::fmod(nu_mid + 10.0 * M_PI,
                                              2.0 * M_PI) - M_PI;
                                double const E_m = toEccentricH(nu_w_m);
                                double       M_m = E_m - e_h * std::sin(E_m);
                                if (M_m < M_exit_h) M_m += 2.0 * M_PI;
                                tof_m = (M_m - M_exit_h) / n_h;
                                nu_m  = nu_w_m;
                            }
                            else
                            {
                                double const F_m =
                                    2.0 * std::atanh(k_h * std::tan(nu_mid / 2.0));
                                double const M_m =
                                    e_h * std::sinh(F_m) - F_m;
                                tof_m = (M_m - M0_h) / n_hyp_h;
                                nu_m  = nu_mid;
                            }

                            double const r_m_mag =
                                p_h / (1.0 + e_h * std::cos(nu_m));
                            glm::dvec3 const r_m =
                                r_m_mag * (std::cos(nu_m) * helio.e_hat +
                                           std::sin(nu_m) * helio.q_hat);
                            auto [tgt_r_m, tgt_v_m] =
                                keplerPropagate(tgt_r_t0, tgt_v_t0,
                                                GM_SUN_SCENE, tof_m);
                            if (glm::length(r_m - tgt_r_m) < target_soi)
                                nu_hi_h = nu_mid;
                            else
                                nu_lo_h = nu_mid;
                        }

                        // Recover refined encounter state on heliocentric orbit
                        double nu_enc, entry_tof_h;
                        if (e_h < 1.0)
                        {
                            double const nu_w_r =
                                std::fmod(nu_hi_h + 10.0 * M_PI, 2.0 * M_PI)
                                - M_PI;
                            double const E_r = toEccentricH(nu_w_r);
                            double       M_r = E_r - e_h * std::sin(E_r);
                            if (M_r < M_exit_h) M_r += 2.0 * M_PI;
                            entry_tof_h = (M_r - M_exit_h) / n_h;
                            nu_enc = nu_w_r;
                        }
                        else
                        {
                            double const F_r =
                                2.0 * std::atanh(k_h * std::tan(nu_hi_h / 2.0));
                            double const M_r = e_h * std::sinh(F_r) - F_r;
                            entry_tof_h = (M_r - M0_h) / n_hyp_h;
                            nu_enc = nu_hi_h;
                        }

                        double const r_enc_mag =
                            p_h / (1.0 + e_h * std::cos(nu_enc));
                        glm::dvec3 const entry_r_sun =
                            r_enc_mag * (std::cos(nu_enc) * helio.e_hat +
                                         std::sin(nu_enc) * helio.q_hat);
                        double const vr_enc =
                            std::sqrt(GM_SUN_SCENE / p_h) * e_h *
                            std::sin(nu_enc);
                        double const vt_enc =
                            std::sqrt(GM_SUN_SCENE / p_h) *
                            (1.0 + e_h * std::cos(nu_enc));
                        glm::dvec3 const entry_v_sun =
                            vr_enc * glm::normalize(entry_r_sun) +
                            vt_enc * glm::normalize(
                                glm::cross(helio.h_hat, entry_r_sun));

                        auto [tgt_r_enc, tgt_v_enc] =
                            keplerPropagate(tgt_r_t0, tgt_v_t0,
                                            GM_SUN_SCENE, entry_tof_h);

                        glm::dvec3 const sc_r_tgt = entry_r_sun - tgt_r_enc;
                        glm::dvec3 const sc_v_tgt = entry_v_sun - tgt_v_enc;

                        OsculatingOrbit const hyp =
                            computeOsculatingOrbit(sc_r_tgt, sc_v_tgt, GM_tgt);
                        if (hyp.e <= 1.0) continue;

                        double const nu_max_hyp =
                            std::acos(std::clamp(-1.0 / hyp.e, -1.0, 1.0))
                            - 1e-4;
                        double const p_hyp_tgt =
                            hyp.a * (1.0 - hyp.e * hyp.e);

                        double const nu_entry_h =
                            std::atan2(glm::dot(hyp.q_hat, sc_r_tgt),
                                       glm::dot(hyp.e_hat, sc_r_tgt));
                        double const nu_step_hyp =
                            (nu_entry_h <= 0.0 ? 1.0 : -1.0) *
                            (2.0 * nu_max_hyp) /
                            (SolarSystemLineObjects::MAX_ENCOUNTER_VERTS * 0.5);

                        std::vector<glm::dvec3> enc_pts;
                        enc_pts.reserve(
                            SolarSystemLineObjects::MAX_ENCOUNTER_VERTS / 2 + 1);

                        double nu_hv = nu_entry_h;
                        for (int v = 0;
                             v <= SolarSystemLineObjects::MAX_ENCOUNTER_VERTS / 2;
                             ++v)
                        {
                            double const r_h_mag =
                                p_hyp_tgt /
                                (1.0 + hyp.e * std::cos(nu_hv));
                            if (r_h_mag > target_soi || r_h_mag <= 0.0) break;

                            enc_pts.push_back(
                                tgt_r_enc +
                                r_h_mag * (std::cos(nu_hv) * hyp.e_hat +
                                           std::sin(nu_hv) * hyp.q_hat));
                            nu_hv += nu_step_hyp;
                        }

                        std::vector<LineVertex> enc_verts;
                        enc_verts.reserve(enc_pts.size() * 2);
                        for (std::size_t v = 0; v + 1 < enc_pts.size(); ++v)
                        {
                            float const t = static_cast<float>(v) /
                                static_cast<float>(std::max(enc_pts.size() - 1, std::size_t{1}));
                            enc_verts.push_back({glm::vec3(enc_pts[v]),
                                                 {1.0f, 0.55f - 0.15f * t, 0.0f, 1.0f}, 0.0f});
                            enc_verts.push_back({glm::vec3(enc_pts[v + 1]),
                                                 {1.0f, 0.55f - 0.15f * t, 0.0f, 1.0f}, 0.0f});
                        }

                        glm::vec3 const tgt_crr =
                            glm::vec3(interpolatedPosition(ss, pi) -
                                      scene.camera.pos_d);
                        found = uploadArc(enc_verts, tgt_crr);
                    }

                    if (!found)
                        enc_obj_early.visible = false;
                }

                return helio_obj.visible;
            }

            // Sub-case 3a: elliptic orbit — scan for moon SOI entries.
            if (!(post_orbit.a > 0.0 && post_orbit.e < 1.0))
                { enc_obj_early.visible = false; return false; }

            int const dom_idx = sc.dominant_body_idx;
            bool found = false;

            for (std::size_t mk = 0; mk < ss.moon_states.size() && !found; ++mk)
            {
                auto const& ms = ss.moon_states[mk];
                if (ms.parent_planet_index != dom_idx) continue;

                auto const& moon_def =
                    ss.defs[static_cast<std::size_t>(dom_idx)]
                           .moons[static_cast<std::size_t>(ms.moon_index)];
                if (moon_def.soi_km <= 0.0) continue;
                double const GM_moon    = G_KM3 * moon_def.mass_kg;
                double const target_soi = moon_def.soi_km;

                glm::dvec3 const moon_world   = moonPositionAtSpacecraftTime(ss, mk, i);
                glm::dvec3 const moon_v_world = interpolatedMoonVelocity(ss, mk);
                glm::dvec3 const moon_r0 = moon_world - ref_pos_phys;
                glm::dvec3 const moon_v0 = moon_v_world - ref_vel;

                auto [moon_r_t0, moon_v_t0] =
                    keplerPropagate(moon_r0, moon_v0, ref_GM, dt_burn);

                double const a_orb = post_orbit.a;
                double const e_orb = post_orbit.e;
                double const p_orb = a_orb * (1.0 - e_orb * e_orb);
                double const n_orb = std::sqrt(ref_GM / (a_orb * a_orb * a_orb));

                double const nu_burn = std::atan2(glm::dot(post_orbit.q_hat, br),
                                                  glm::dot(post_orbit.e_hat, br));

                auto toEccentric = [&](double nu) {
                    return 2.0 * std::atan(
                        std::sqrt((1.0 - e_orb) / (1.0 + e_orb)) * std::tan(nu / 2.0));
                };
                double const E_burn = toEccentric(nu_burn);
                double const M_burn = E_burn - e_orb * std::sin(E_burn);

                constexpr int N_SCAN = 360;
                int    entry_idx   = -1;
                double entry_tof   = 0.0;
                glm::dvec3 entry_r_earth{};
                glm::dvec3 entry_v_earth{};

                for (int s = 0; s < N_SCAN; ++s)
                {
                    double const nu   = nu_burn + (2.0 * M_PI * s) / N_SCAN;
                    double const nu_w = std::fmod(nu + 10.0 * M_PI, 2.0 * M_PI) - M_PI;

                    double const E_s = toEccentric(nu_w);
                    double       M_s = E_s - e_orb * std::sin(E_s);
                    if (M_s < M_burn) M_s += 2.0 * M_PI;
                    double const tof = (M_s - M_burn) / n_orb;

                    double     const r_s_mag = p_orb / (1.0 + e_orb * std::cos(nu_w));
                    glm::dvec3 const r_s     = r_s_mag *
                        (std::cos(nu_w) * post_orbit.e_hat +
                         std::sin(nu_w) * post_orbit.q_hat);

                    auto [moon_r_s, moon_v_s] =
                        keplerPropagate(moon_r_t0, moon_v_t0, ref_GM, tof);

                    if (glm::length(r_s - moon_r_s) < target_soi)
                    {
                        entry_idx = s;
                        break;
                    }
                }

                if (entry_idx < 0) continue;

                // Bisect to find a smoothly-varying entry point
                {
                    double nu_lo = nu_burn + (2.0 * M_PI * std::max(entry_idx - 1, 0)) / N_SCAN;
                    double nu_hi = nu_burn + (2.0 * M_PI * entry_idx) / N_SCAN;

                    for (int b = 0; b < 24; ++b)
                    {
                        double const nu_mid  = 0.5 * (nu_lo + nu_hi);
                        double const nu_w_m  = std::fmod(nu_mid + 10.0 * M_PI, 2.0 * M_PI) - M_PI;

                        double const E_m = toEccentric(nu_w_m);
                        double       M_m = E_m - e_orb * std::sin(E_m);
                        if (M_m < M_burn) M_m += 2.0 * M_PI;
                        double const tof_m   = (M_m - M_burn) / n_orb;

                        double     const r_m_mag = p_orb / (1.0 + e_orb * std::cos(nu_w_m));
                        glm::dvec3 const r_m     = r_m_mag *
                            (std::cos(nu_w_m) * post_orbit.e_hat +
                             std::sin(nu_w_m) * post_orbit.q_hat);

                        auto [moon_r_m, moon_v_m] =
                            keplerPropagate(moon_r_t0, moon_v_t0, ref_GM, tof_m);

                        if (glm::length(r_m - moon_r_m) < target_soi)
                            nu_hi = nu_mid;
                        else
                            nu_lo = nu_mid;
                    }

                    double const nu_w_r  = std::fmod(nu_hi + 10.0 * M_PI, 2.0 * M_PI) - M_PI;
                    double const E_r     = toEccentric(nu_w_r);
                    double       M_r     = E_r - e_orb * std::sin(E_r);
                    if (M_r < M_burn) M_r += 2.0 * M_PI;
                    entry_tof     = (M_r - M_burn) / n_orb;

                    double const r_r_mag = p_orb / (1.0 + e_orb * std::cos(nu_w_r));
                    entry_r_earth = r_r_mag *
                        (std::cos(nu_w_r) * post_orbit.e_hat +
                         std::sin(nu_w_r) * post_orbit.q_hat);

                    double const vr = std::sqrt(ref_GM / p_orb) * e_orb * std::sin(nu_w_r);
                    double const vt = std::sqrt(ref_GM / p_orb) * (1.0 + e_orb * std::cos(nu_w_r));
                    entry_v_earth = vr * glm::normalize(entry_r_earth) +
                                   vt * glm::normalize(glm::cross(post_orbit.h_hat, entry_r_earth));
                }

                auto [moon_r_enc, moon_v_enc] =
                    keplerPropagate(moon_r_t0, moon_v_t0, ref_GM, entry_tof);

                glm::dvec3 const sc_r_moon = entry_r_earth - moon_r_enc;
                glm::dvec3 const sc_v_moon = entry_v_earth - moon_v_enc;

                OsculatingOrbit const hyp =
                    computeOsculatingOrbit(sc_r_moon, sc_v_moon, GM_moon);
                if (hyp.e <= 1.0) continue;

                double const nu_max = std::acos(-1.0 / hyp.e) - 1e-4;
                double const p_hyp  = hyp.a * (1.0 - hyp.e * hyp.e);

                double const nu_entry_h = std::atan2(glm::dot(hyp.q_hat, sc_r_moon),
                                                      glm::dot(hyp.e_hat, sc_r_moon));
                double const nu_step =
                    (nu_entry_h <= 0.0 ? 1.0 : -1.0) *
                    (2.0 * nu_max) /
                    (SolarSystemLineObjects::MAX_ENCOUNTER_VERTS * 0.5);

                std::vector<glm::dvec3> moon_enc_pts;
                moon_enc_pts.reserve(SolarSystemLineObjects::MAX_ENCOUNTER_VERTS / 2 + 1);

                double nu_h = nu_entry_h;
                for (int v = 0; v <= SolarSystemLineObjects::MAX_ENCOUNTER_VERTS / 2; ++v)
                {
                    double const r_h_mag = p_hyp / (1.0 + hyp.e * std::cos(nu_h));
                    if (r_h_mag > target_soi || r_h_mag <= 0.0) break;
                    moon_enc_pts.push_back(
                        moon_r_enc + r_h_mag * (std::cos(nu_h) * hyp.e_hat +
                                                  std::sin(nu_h) * hyp.q_hat));
                    nu_h += nu_step;
                }

                std::vector<LineVertex> enc_verts;
                enc_verts.reserve(moon_enc_pts.size() * 2);
                for (std::size_t v = 0; v + 1 < moon_enc_pts.size(); ++v)
                {
                    float const t = static_cast<float>(v) /
                        static_cast<float>(std::max(moon_enc_pts.size() - 1, std::size_t{1}));
                    enc_verts.push_back({glm::vec3(moon_enc_pts[v]),
                                         {1.0f, 0.55f - 0.15f * t, 0.0f, 1.0f}, 0.0f});
                    enc_verts.push_back({glm::vec3(moon_enc_pts[v + 1]),
                                         {1.0f, 0.55f - 0.15f * t, 0.0f, 1.0f}, 0.0f});
                }

                found = uploadArc(enc_verts, ref_crr);
            }

            if (!found)
                scene.objs[line_objs.sc_encounter_path_obj_ids[i]].visible = false;
            return found;
        };

        // Build the escape hyperbola arc in planet-relative coordinates (Earth-centric).
        // Vertices are the standard hyperbolic conic points relative to the dominant body.
        // Caller sets path_obj.position to the planet's CRR position.
        auto buildEscapeArc = [&](
            OsculatingOrbit const& orb,
            glm::dvec3 const&      br,
            double                 planet_gm,
            double                 soi_km) -> std::vector<LineVertex>
        {
            double const e = orb.e;
            double const a = orb.a;
            double const p = a * (1.0 - e * e);
            if (p <= 0.0) return {};

            double const cos_nue = std::clamp((p / soi_km - 1.0) / e, -1.0, 1.0);
            double const nu_exit = std::acos(cos_nue);
            double const nu_burn = std::atan2(glm::dot(orb.q_hat, br),
                                              glm::dot(orb.e_hat, br));
            if (nu_exit <= nu_burn) return {};

            constexpr int N = SolarSystemLineObjects::MAX_PATH_VERTS / 2 - 1;
            std::vector<glm::dvec3> pts;
            pts.reserve(N + 1);

            for (int k = 0; k <= N; ++k)
            {
                double const nu  = nu_burn + (nu_exit - nu_burn) * k / N;
                double const r_m = p / (1.0 + e * std::cos(nu));
                if (r_m <= 0.0 || r_m > soi_km * 1.01) break;
                pts.push_back(r_m * (std::cos(nu) * orb.e_hat + std::sin(nu) * orb.q_hat));
            }

            std::vector<LineVertex> verts;
            verts.reserve(pts.size() * 2);
            int const n_seg = static_cast<int>(pts.size()) - 1;
            for (int k = 0; k < n_seg; ++k)
            {
                float const t = static_cast<float>(k) /
                    static_cast<float>(std::max(n_seg, 1));
                glm::vec4 const col = glm::mix(
                    glm::vec4(1.0f, 0.9f, 0.3f, 0.9f),
                    glm::vec4(0.5f, 0.45f, 0.15f, 0.15f), t);
                verts.push_back({glm::vec3(pts[k]),     col, t});
                verts.push_back({glm::vec3(pts[k + 1]), col, t});
            }
            return verts;
        };

        // Whether any approved maneuver is pending for this spacecraft.
        // Used to decide whether the live path or the maneuver block owns the encounter arc.
        bool const has_pending_maneuver = is_maneuver_sc ||
            std::any_of(sc.maneuvers.begin(), sc.maneuvers.end(),
                        [](ManeuverNode const& n){ return n.approved; });

        // --- Predicted path (analytical Keplerian, rebuilt every frame for smoothness) ---
        // Using the same consistent (r_rel, v_rel) as the orbit ring avoids any
        // time_accumulator-based drift or oscillation.
        auto& path_obj = scene.objs[line_objs.sc_path_obj_ids[i]];
        path_obj.position = ref_crr;

        bool const is_hyperbolic = orbit.e >= 1.0 || orbit.a <= 0.0;
        bool const path_active = ss.show_spacecraft_path || is_hyperbolic;
        path_obj.visible = path_active;

        if (path_active)
        {
            double const p_path = orbit.a * (1.0 - orbit.e * orbit.e);
            std::vector<LineVertex> pv;

            if (p_path > 0.0)
            {
                double const nu0 = std::atan2(glm::dot(r_rel, orbit.q_hat),
                                              glm::dot(r_rel, orbit.e_hat));
                auto sampleOrbit = [&](double nu) -> glm::dvec3 {
                    double const r = p_path / (1.0 + orbit.e * std::cos(nu));
                    return r * (std::cos(nu) * orbit.e_hat + std::sin(nu) * orbit.q_hat);
                };

                constexpr int N = 300;
                std::vector<glm::dvec3> pts;
                pts.reserve(N + 1);

                if (orbit.e >= 1.0)
                {
                    double const cos_nu_exit = std::clamp((p_path / soi_exit_km - 1.0) / orbit.e,
                                                          -1.0, 1.0);
                    double const nu_exit = std::acos(cos_nu_exit);
                    if (nu_exit - nu0 >= 1e-9)
                    {
                        for (int k = 0; k <= N; ++k)
                        {
                            double const    nu = nu0 + (nu_exit - nu0) * k / N;
                            glm::dvec3 const pt = sampleOrbit(nu);
                            if (glm::length(pt) < ref_radius_km) break;
                            pts.push_back(pt);
                        }
                    }
                }
                else
                {
                    double const nu_end = nu0 + 2.0 * M_PI;
                    for (int k = 0; k <= N; ++k)
                    {
                        double const    nu   = nu0 + (nu_end - nu0) * k / N;
                        glm::dvec3 const pt   = sampleOrbit(nu);
                        double     const dist = glm::length(pt);
                        if (dist < ref_radius_km) break;
                        if (dist > soi_exit_km)   break;
                        pts.push_back(pt);
                    }
                }

                int const max_segs = (SolarSystemLineObjects::MAX_PATH_VERTS / 2) - 1;
                int const n        = std::min(static_cast<int>(pts.size()) - 1, max_segs);
                pv.reserve(n * 2);
                for (int s = 0; s < n; ++s)
                {
                    float t   = static_cast<float>(s) / static_cast<float>(std::max(n - 1, 1));
                    glm::vec4 col = glm::mix(glm::vec4(1.0f, 0.9f, 0.3f, 0.9f),
                                             glm::vec4(0.5f, 0.45f, 0.15f, 0.15f), t);
                    pv.push_back({glm::vec3(pts[s]),     col, t});
                    pv.push_back({glm::vec3(pts[s + 1]), col, t});
                }
            }

            path_obj.indices_size = static_cast<uint32_t>(pv.size());
            if (!pv.empty())
            {
                auto& pvbuf = line_objs.sc_path_vbufs[i];
                vk::DeviceSize vsz = sizeof(LineVertex) * pv.size();
                void* vptr = pvbuf.memory.mapMemory(0, vsz).value;
                std::memcpy(vptr, pv.data(), static_cast<std::size_t>(vsz));
                pvbuf.memory.unmapMemory();
            }
        }

        // Live encounter arc: show on current orbit when no maneuver is pending.
        // The maneuver block below will overwrite this when a maneuver is active.
        if (!has_pending_maneuver)
            drawEncounterArc(r_rel, orbit, 1.0, 0.0);

        // --- Maneuver visuals (ghost, post-burn ring, node marker) ---

        if (i < line_objs.sc_maneuver_orbit_obj_ids.size())
        {
            auto& mo = scene.objs[line_objs.sc_maneuver_orbit_obj_ids[i]];
            if (is_maneuver_sc)
            {
                // Propagate orbit to burn time relative to DOMINANT body
                auto [br, bv] = keplerPropagate(r_phys, v_rel, ref_GM,
                                                ss.maneuver_t0_s);

                // Decompose reference velocity at t0 into PRN frame
                glm::dvec3 const pg_hat = glm::length(bv) > 1e-15
                    ? glm::normalize(bv) : glm::dvec3(0.0, 1.0, 0.0);
                glm::dvec3 const rd_hat = glm::length(br) > 1e-15
                    ? glm::normalize(br) : glm::dvec3(1.0, 0.0, 0.0);
                glm::dvec3 const nm_hat = glm::normalize(glm::cross(br, bv));
                double const ref_pg = glm::dot(bv, pg_hat);
                double const ref_rd = glm::dot(bv, rd_hat);
                double const ref_nm = glm::dot(bv, nm_hat);

                auto& ss_mut = const_cast<SolarSystem&>(ss);
                ss_mut.maneuver_ref_prograde = ref_pg;
                ss_mut.maneuver_ref_radial   = ref_rd;
                ss_mut.maneuver_ref_normal   = ref_nm;
                if (!ss_mut.maneuver_targets_initialized)
                {
                    ss_mut.maneuver_prograde            = 0.0;
                    ss_mut.maneuver_radial              = 0.0;
                    ss_mut.maneuver_normal              = 0.0;
                    ss_mut.maneuver_targets_initialized = true;
                }

                glm::dvec3 const dv = dvWorld(br, bv,
                    ss.maneuver_prograde,
                    ss.maneuver_radial,
                    ss.maneuver_normal);
                glm::dvec3 const bv_post = bv + dv;

                if (!ss_mut.spacecraft_states[i].maneuvers.empty() &&
                    !ss_mut.spacecraft_states[i].maneuvers.back().approved)
                {
                    auto& node = ss_mut.spacecraft_states[i].maneuvers.back();
                    node.burn_pos_rel              = br;
                    node.burn_vel_rel              = bv;
                    node.delta_v_world             = dv;
                    node.burn_dominant_body_idx    = sc.dominant_body_idx;
                    node.burn_dominant_is_moon     = sc.dominant_is_moon;
                    node.burn_dominant_moon_idx    = sc.dominant_moon_idx;
                }

                OsculatingOrbit const post_orbit = computeOsculatingOrbit(br, bv_post, ref_GM);
                double const dv_mag = glm::length(dv);
                mo.position          = ref_crr;
                mo.rotation_override = orbitRingMatrix(post_orbit);
                mo.visible           = post_orbit.a > 0.0 && post_orbit.e < 1.0 && dv_mag > 0.0;

                drawEncounterArc(br, post_orbit, dv_mag, ss.maneuver_t0_s);

                // When the planned burn escapes the current SOI, override the path
                // buffer with the post-burn hyperbolic arc (planet-relative).
                if (dv_mag > 0.0 && !sc.dominant_is_moon &&
                    sc.dominant_body_idx > 0 &&
                    post_orbit.e >= 1.0 && post_orbit.a < 0.0)
                {
                    auto esc_verts = buildEscapeArc(post_orbit, br, ref_GM, soi_exit_km);

                    if (!esc_verts.empty())
                    {
                        auto& pvbuf = line_objs.sc_path_vbufs[i];
                        vk::DeviceSize const vsz =
                            sizeof(LineVertex) * esc_verts.size();
                        void* vptr = pvbuf.memory.mapMemory(0, vsz).value;
                        std::memcpy(vptr, esc_verts.data(),
                                    static_cast<std::size_t>(vsz));
                        pvbuf.memory.unmapMemory();
                        path_obj.indices_size =
                            static_cast<uint32_t>(esc_verts.size());
                        path_obj.position = ref_crr;
                        path_obj.visible  = true;
                    }
                }
            }
            else
            {
                // Show approved maneuver orbit and arcs using stored burn state
                auto const& sc2 = ss.spacecraft_states[i];
                bool has_node = false;
                for (auto const& node : sc2.maneuvers)
                {
                    if (!node.approved) continue;
                    if (glm::length(node.delta_v_world) < 1e-12) continue;
                    has_node = true;

                    // If the spacecraft has escaped the burn body's SOI, temporarily
                    // restore the burn-time dominant body so drawEncounterArc routes
                    // to the correct case and uses the correct GM / SOI radius.
                    bool const soi_changed =
                        (sc.dominant_body_idx != node.burn_dominant_body_idx) ||
                        (sc.dominant_is_moon   != node.burn_dominant_is_moon);

                    glm::dvec3 saved_ref_pos_phys  = ref_pos_phys;
                    glm::dvec3 saved_ref_pos_render = ref_pos_render;
                    glm::dvec3 saved_ref_vel        = ref_vel;
                    double     saved_ref_GM         = ref_GM;
                    double     saved_ref_radius_km  = ref_radius_km;
                    double     saved_soi_exit_km    = soi_exit_km;
                    int        saved_dom_body        = sc.dominant_body_idx;
                    bool       saved_dom_is_moon     = sc.dominant_is_moon;
                    int        saved_dom_moon        = sc.dominant_moon_idx;

                    if (soi_changed)
                    {
                        if (node.burn_dominant_is_moon &&
                            node.burn_dominant_moon_idx >= 0 &&
                            node.burn_dominant_moon_idx <
                                static_cast<int>(ss.moon_states.size()))
                        {
                            std::size_t const mk   =
                                static_cast<std::size_t>(node.burn_dominant_moon_idx);
                            auto const& ms_b       = ss.moon_states[mk];
                            auto const& mdef_b     =
                                ss.defs[ms_b.parent_planet_index]
                                       .moons[ms_b.moon_index];
                            ref_pos_phys   = moonPositionAtSpacecraftTime(ss, mk, i);
                            ref_pos_render = interpolatedMoonPosition(ss, mk);
                            ref_vel        = interpolatedMoonVelocity(ss, mk);
                            ref_GM         = G_KM3 * mdef_b.mass_kg;
                            ref_radius_km  = mdef_b.radius_km;
                            soi_exit_km    = mdef_b.soi_km;
                        }
                        else if (node.burn_dominant_body_idx == 0)
                        {
                            ref_pos_phys   = glm::dvec3(0.0);
                            ref_pos_render = glm::dvec3(0.0);
                            ref_vel        = glm::dvec3(0.0);
                            ref_GM         = GM_SUN_SCENE;
                            ref_radius_km  = ss.defs[0].radius_km;
                            soi_exit_km    = 1e13;
                        }
                        else
                        {
                            std::size_t const bi =
                                static_cast<std::size_t>(node.burn_dominant_body_idx);
                            ref_pos_phys   = planetPositionAtSpacecraftTime(ss, bi, i);
                            ref_pos_render = interpolatedPosition(ss, bi);
                            ref_vel        = interpolatedVelocity(ss, bi);
                            ref_GM         = G_KM3 * ss.defs[bi].mass_kg;
                            ref_radius_km  = ss.defs[bi].radius_km;
                            soi_exit_km    =
                                ss.defs[bi].soi_km > 0.0 ? ss.defs[bi].soi_km : 1e13;
                        }
                        // sc is a const& alias into spacecraft_states — override so
                        // the lambda sees the correct case routing.
                        auto& sc_mut2 = const_cast<SolarSystem&>(ss).spacecraft_states[i];
                        sc_mut2.dominant_body_idx = node.burn_dominant_body_idx;
                        sc_mut2.dominant_is_moon  = node.burn_dominant_is_moon;
                        sc_mut2.dominant_moon_idx = node.burn_dominant_moon_idx;
                    }

                    glm::vec3 const burn_ref_crr =
                        glm::vec3(ref_pos_render - scene.camera.pos_d);

                    glm::dvec3 const bv_post = node.burn_vel_rel + node.delta_v_world;
                    OsculatingOrbit const aorbit = computeOsculatingOrbit(
                        node.burn_pos_rel, bv_post, ref_GM);

                    // Orbit ring only for elliptic post-burn orbits
                    if (aorbit.a > 0.0 && aorbit.e < 1.0)
                    {
                        mo.position          = burn_ref_crr;
                        mo.rotation_override = orbitRingMatrix(aorbit);
                        mo.visible           = true;
                    }
                    else
                    {
                        mo.visible = false;
                    }

                    // Always call drawEncounterArc regardless of orbit type so that
                    // heliocentric arc position is updated each frame (CRR) and
                    // the arc remains visible for escape trajectories.
                    double const dt_to_burn =
                        node.t0_abs_s - ss.elapsed_simulation_s;

                    // For completed burns (dt_to_burn < 0), ref_pos_phys is the
                    // planet's CURRENT position, but the lambda needs the planet's
                    // position at the burn epoch as its starting point.  Propagate
                    // backward so that passing dt_burn=0 lands exactly at burn time.
                    if (dt_to_burn < 0.0 &&
                        node.burn_dominant_body_idx > 0 &&
                        !node.burn_dominant_is_moon)
                    {
                        auto [r_burn, v_burn] = keplerPropagate(
                            ref_pos_phys, ref_vel, GM_SUN_SCENE, dt_to_burn);
                        ref_pos_phys = r_burn;
                        ref_vel      = v_burn;
                    }

                    drawEncounterArc(node.burn_pos_rel, aorbit,
                                     glm::length(node.delta_v_world),
                                     std::max(dt_to_burn, 0.0));

                    // For escape maneuvers: keep the escape arc in the path buffer,
                    // rendered in heliocentric space so it connects to the helio orbit arc.
                    // ref_pos_phys is already at burn epoch (backward-propagated above).
                    //
                    // Skip when the spacecraft has already entered a different planet's SOI:
                    // path_obj was already set above with the correct hyperbolic approach arc
                    // for that planet, and overwriting it with the old departure arc is wrong.
                    bool const in_new_planet_soi = soi_changed &&
                        saved_dom_body > 0 && !saved_dom_is_moon;
                    if (!in_new_planet_soi &&
                        aorbit.e >= 1.0 && aorbit.a < 0.0 &&
                        !node.burn_dominant_is_moon && node.burn_dominant_body_idx > 0)
                    {
                        auto esc_verts = buildEscapeArc(
                            aorbit, node.burn_pos_rel, ref_GM, soi_exit_km);

                        if (!esc_verts.empty())
                        {
                            auto& pvbuf = line_objs.sc_path_vbufs[i];
                            vk::DeviceSize const vsz =
                                sizeof(LineVertex) * esc_verts.size();
                            void* vptr = pvbuf.memory.mapMemory(0, vsz).value;
                            std::memcpy(vptr, esc_verts.data(),
                                        static_cast<std::size_t>(vsz));
                            pvbuf.memory.unmapMemory();
                            path_obj.indices_size =
                                static_cast<uint32_t>(esc_verts.size());
                            path_obj.position = burn_ref_crr;
                            path_obj.visible  = true;
                        }
                    }

                    // Restore ref vars and sc SOI fields if we temporarily overrode them.
                    if (soi_changed)
                    {
                        ref_pos_phys   = saved_ref_pos_phys;
                        ref_pos_render = saved_ref_pos_render;
                        ref_vel        = saved_ref_vel;
                        ref_GM         = saved_ref_GM;
                        ref_radius_km  = saved_ref_radius_km;
                        soi_exit_km    = saved_soi_exit_km;
                        auto& sc_restore = const_cast<SolarSystem&>(ss).spacecraft_states[i];
                        sc_restore.dominant_body_idx = saved_dom_body;
                        sc_restore.dominant_is_moon  = saved_dom_is_moon;
                        sc_restore.dominant_moon_idx = saved_dom_moon;
                    }

                    break;
                }
                if (!has_node)
                {
                    mo.visible = false;
                    if (i < line_objs.sc_encounter_path_obj_ids.size())
                        scene.objs[line_objs.sc_encounter_path_obj_ids[i]].visible = false;
                    if (i < line_objs.sc_helio_orbit_obj_ids.size())
                        scene.objs[line_objs.sc_helio_orbit_obj_ids[i]].visible = false;
                }
            }
        }
    }

    // Helper: compute dominant body (pos_phys, pos_render, vel, GM) for any spacecraft.
    auto scDomRef = [&](std::size_t sci) {
        auto const& sc2 = ss.spacecraft_states[sci];
        struct Ref { glm::dvec3 pos_phys, pos_render, vel; double gm; };
        if (sc2.dominant_is_moon && sc2.dominant_moon_idx >= 0 &&
            sc2.dominant_moon_idx < static_cast<int>(ss.moon_states.size()))
        {
            std::size_t const mk = static_cast<std::size_t>(sc2.dominant_moon_idx);
            auto const& ms  = ss.moon_states[mk];
            auto const& mdf = ss.defs[ms.parent_planet_index].moons[ms.moon_index];
            return Ref{moonPositionAtSpacecraftTime(ss, mk, sci),
                       interpolatedMoonPosition(ss, mk),
                       interpolatedMoonVelocity(ss, mk),
                       G_KM3 * mdf.mass_kg};
        }
        else if (sc2.dominant_body_idx == 0)
        {
            return Ref{glm::dvec3(0.0), glm::dvec3(0.0), glm::dvec3(0.0), GM_SUN_SCENE};
        }
        else
        {
            std::size_t const bi = static_cast<std::size_t>(sc2.dominant_body_idx);
            return Ref{planetPositionAtSpacecraftTime(ss, bi, sci),
                       interpolatedPosition(ss, bi),
                       interpolatedVelocity(ss, bi),
                       G_KM3 * ss.defs[bi].mass_kg};
        }
    };

    // Burn node marker — visible during planning AND for approved nodes
    if (line_objs.maneuver_node_obj_id >= 0)
    {
        auto& no = scene.objs[line_objs.maneuver_node_obj_id];
        no.visible = false;

        if (ss.maneuver_mode && ss.maneuver_sc_idx >= 0 &&
            ss.maneuver_sc_idx < static_cast<int>(ss.spacecraft_states.size()))
        {
            std::size_t const sci = static_cast<std::size_t>(ss.maneuver_sc_idx);
            auto const& sc2 = ss.spacecraft_states[sci];
            auto ref = scDomRef(sci);
            glm::dvec3 r2 = sc2.position_km - ref.pos_phys;
            glm::dvec3 v2 = sc2.velocity_km - ref.vel;
            auto [br, bv] = keplerPropagate(r2, v2, ref.gm, ss.maneuver_t0_s);
            no.position = glm::vec3(ref.pos_render + br - scene.camera.pos_d);
            no.visible  = true;
        }
        else
        {
            for (std::size_t sci = 0; sci < ss.spacecraft_states.size(); ++sci)
            {
                for (auto const& node : ss.spacecraft_states[sci].maneuvers)
                {
                    if (!node.approved) continue;
                    auto ref = scDomRef(sci);
                    no.position = glm::vec3(ref.pos_render + node.burn_pos_rel - scene.camera.pos_d);
                    no.visible  = true;
                    goto node_done;
                }
            }
            node_done:;
        }
    }

    // Ghost spacecraft
    if (line_objs.maneuver_ghost_obj_id >= 0)
    {
        auto& go = scene.objs[line_objs.maneuver_ghost_obj_id];
        if (ss.maneuver_mode && ss.maneuver_sc_idx >= 0 &&
            ss.maneuver_sc_idx < static_cast<int>(ss.spacecraft_states.size()))
        {
            std::size_t const sci = static_cast<std::size_t>(ss.maneuver_sc_idx);
            auto const& sc2 = ss.spacecraft_states[sci];
            auto ref = scDomRef(sci);
            glm::dvec3 r2 = sc2.position_km - ref.pos_phys;
            glm::dvec3 v2 = sc2.velocity_km - ref.vel;
            auto [br, bv] = keplerPropagate(r2, v2, ref.gm, ss.maneuver_t0_s);
            go.position          = glm::vec3(ref.pos_render + br - scene.camera.pos_d);
            go.scale             = static_cast<float>(ss.spacecraft_defs[sci].visual_scale_km);
            go.rotation_override = scene.objs[sc2.scene_object_index].rotation_override;
            go.visible           = true;
        }
        else
        {
            go.visible = false;
        }
    }
}

void updateSunLighting(Scene& scene, Camera const& cam)
{
    glm::vec3 const sun_cam_rel = glm::vec3(-cam.pos_d);
    scene.light.position = sun_cam_rel;
    scene.light.sun_pos  = glm::length(sun_cam_rel) > 0.0f
                           ? glm::normalize(sun_cam_rel)
                           : glm::vec3(1.0f, 0.0f, 0.0f);
}

// ---------------------------------------------------------------------------
// Spacecraft scene objects
// ---------------------------------------------------------------------------

void initSpacecraftObjects(Scene& scene, SolarSystem& ss,
                           DrawableMesh const& mesh, Camera const& cam,
                           SolarSystemLineObjects& line_objs)
{
    Material const mat{
        .name = {"Spacecraft"},
        .program = 2,
        .shader_data = {}
    };

    for (std::size_t i = 0; i < ss.spacecraft_defs.size(); ++i)
    {
        auto const& def   = ss.spacecraft_defs[i];
        auto&       state = ss.spacecraft_states[i];

        if (state.scene_object_index >= 0) continue; // already in scene

        Object obj = createObject(mesh);
        obj.material  = mat;
        obj.position  = glm::vec3(state.position_km - cam.pos_d);
        obj.scale     = static_cast<float>(def.visual_scale_km);
        obj.rotation_override = glm::mat4(1.0f);
        obj.visible   = true;

        state.scene_object_index = static_cast<int>(scene.objs.size());
        addObject(scene, obj);
    }

    // Ghost spacecraft: one shared object at maneuver burn position.
    // Added last so its gl_BaseInstance falls after all real spacecraft.
    if (line_objs.maneuver_ghost_obj_id < 0 && !ss.spacecraft_defs.empty())
    {
        Object ghost = createObject(mesh);
        ghost.material        = mat;
        ghost.position        = glm::vec3(0.0f);
        ghost.scale           = static_cast<float>(ss.spacecraft_defs[0].visual_scale_km);
        ghost.rotation_override = glm::mat4(1.0f);
        ghost.visible         = false;

        line_objs.maneuver_ghost_obj_id = static_cast<int>(scene.objs.size());
        addObject(scene, ghost);
    }
}

void writeSpacecraftMaterialBuffers(Scene& scene, SolarSystem const& ss,
                                    SolarSystemLineObjects const& line_objs, int frame)
{
    // Compute the same base_index as writePlanetMaterialBuffers
    int base_index = 0;
    for (auto const& [prog, obj_list] : scene.programs)
    {
        if (prog >= 2) break;
        base_index += static_cast<int>(obj_list.size());
    }

    int const planet_count = static_cast<int>(ss.defs.size());
    int moon_count = 0;
    for (auto const& def : ss.defs)
        moon_count += static_cast<int>(def.moons.size());

    for (int j = 0; j < static_cast<int>(ss.spacecraft_defs.size()); ++j)
    {
        auto const& def = ss.spacecraft_defs[j];

        PlanetMaterialData mat;
        mat.diffuse_texture        = -1;
        mat.normal_texture         = -1;
        mat.has_normal_map         = 0;
        mat.has_atmosphere         = 0;
        mat.atmosphere_color_scale = glm::vec4(0.0f);
        mat.albedo_color           = glm::vec4(def.color, 1.0f);
        mat.roughness              = 0.5f;
        mat.metallic               = 0.0f;
        mat.emissive               = 1.0f;
        mat.cloud_texture          = -1;

        writeBuffer(*scene.planet_material_buffer[frame], mat,
                    base_index + planet_count + moon_count + j);
    }

    // Ghost spacecraft material: golden-orange tint
    if (line_objs.maneuver_ghost_obj_id >= 0)
    {
        int const ghost_slot = planet_count + moon_count + static_cast<int>(ss.spacecraft_defs.size());
        PlanetMaterialData ghost_mat;
        ghost_mat.diffuse_texture        = -1;
        ghost_mat.normal_texture         = -1;
        ghost_mat.has_normal_map         = 0;
        ghost_mat.has_atmosphere         = 0;
        ghost_mat.atmosphere_color_scale = glm::vec4(0.0f);
        ghost_mat.albedo_color           = glm::vec4(1.0f, 0.5f, 0.1f, 1.0f);
        ghost_mat.roughness              = 0.5f;
        ghost_mat.metallic               = 0.0f;
        ghost_mat.emissive               = 1.0f;
        ghost_mat.cloud_texture          = -1;
        writeBuffer(*scene.planet_material_buffer[frame], ghost_mat, base_index + ghost_slot);
    }
}
