#include "SolarSystemScene.h"

#include "Object.h"
#include "Material.h"
#include "Physics.h"
#include "Pipelines/Planet.h"
#include "Spacecraft.h"
#include "Maneuver.h"

#include <glm/gtc/quaternion.hpp>

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
        soi_exit_km   = SOI_MOON_KM;
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
        soi_exit_km   = (bi == 3) ? SOI_EARTH_KM : 1e13;
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
            soi_exit_km    = SOI_MOON_KM;
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
            soi_exit_km    = (sc.dominant_body_idx == 3) ? SOI_EARTH_KM : 1e13;
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

        // --- Maneuver visuals (ghost, post-burn ring, node marker) ---
        bool const is_maneuver_sc = ss.maneuver_mode &&
                                    ss.maneuver_sc_idx == static_cast<int>(i);

        // Lambda: compute and upload the encounter arc given burn-point state.
        // dt_moon = seconds from current sim state to burn (Moon propagation offset).
        // Returns true if an arc was found and uploaded; hides the object if false.
        auto drawEncounterArc = [&](glm::dvec3 const& br,
                                    OsculatingOrbit const& post_orbit,
                                    double dv_mag,
                                    double dt_moon) -> bool
        {
            if (!(i < line_objs.sc_encounter_path_obj_ids.size())) return false;
            if (!(post_orbit.a > 0.0 && post_orbit.e < 1.0 && dv_mag > 1e-9)) return false;
            if (sc.dominant_is_moon || sc.dominant_body_idx <= 0) return false;

            int const dom_idx = sc.dominant_body_idx;
            bool found = false;

            for (std::size_t mk = 0; mk < ss.moon_states.size() && !found; ++mk)
            {
                auto const& ms = ss.moon_states[mk];
                if (ms.parent_planet_index != dom_idx) continue;

                auto const& moon_def =
                    ss.defs[static_cast<std::size_t>(dom_idx)]
                           .moons[static_cast<std::size_t>(ms.moon_index)];
                double const GM_moon = G_KM3 * moon_def.mass_kg;

                glm::dvec3 const moon_world   = moonPositionAtSpacecraftTime(ss, mk, i);
                glm::dvec3 const moon_v_world = interpolatedMoonVelocity(ss, mk);
                glm::dvec3 const moon_r0 = moon_world - ref_pos_phys;
                glm::dvec3 const moon_v0 = moon_v_world - ref_vel;

                auto [moon_r_t0, moon_v_t0] =
                    keplerPropagate(moon_r0, moon_v0, ref_GM, dt_moon);

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

                    if (glm::length(r_s - moon_r_s) < SOI_MOON_KM)
                    {
                        entry_idx     = s;
                        entry_tof     = tof;
                        entry_r_earth = r_s;
                        double const vr = std::sqrt(ref_GM / p_orb) * e_orb * std::sin(nu_w);
                        double const vt = std::sqrt(ref_GM / p_orb) * (1.0 + e_orb * std::cos(nu_w));
                        entry_v_earth = vr * glm::normalize(r_s) +
                                       vt * glm::normalize(glm::cross(post_orbit.h_hat, r_s));
                        break;
                    }
                }

                if (entry_idx < 0) continue;

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
                    static_cast<double>(SolarSystemLineObjects::MAX_ENCOUNTER_VERTS - 1);

                std::vector<LineVertex> enc_verts;
                enc_verts.reserve(SolarSystemLineObjects::MAX_ENCOUNTER_VERTS);

                double nu_h = nu_entry_h;
                for (int v = 0; v < SolarSystemLineObjects::MAX_ENCOUNTER_VERTS; ++v)
                {
                    double const r_h_mag = p_hyp / (1.0 + hyp.e * std::cos(nu_h));
                    if (r_h_mag > SOI_MOON_KM || r_h_mag <= 0.0) break;

                    glm::dvec3 const r_h =
                        r_h_mag * (std::cos(nu_h) * hyp.e_hat +
                                   std::sin(nu_h) * hyp.q_hat);
                    glm::dvec3 const r_planet = moon_r_enc + r_h;

                    float const t = static_cast<float>(v) /
                        static_cast<float>(SolarSystemLineObjects::MAX_ENCOUNTER_VERTS);
                    glm::vec4 const col{1.0f, 0.55f - 0.15f * t, 0.0f, 1.0f};

                    enc_verts.push_back({glm::vec3(r_planet), col});
                    nu_h += nu_step;
                }

                if (enc_verts.size() < 2) continue;

                auto& evbuf  = line_objs.sc_encounter_path_vbufs[i];
                vk::DeviceSize const vsz = sizeof(LineVertex) * enc_verts.size();
                void* vptr = evbuf.memory.mapMemory(0, vsz).value;
                std::memcpy(vptr, enc_verts.data(), static_cast<std::size_t>(vsz));
                evbuf.memory.unmapMemory();

                auto& enc_obj        = scene.objs[line_objs.sc_encounter_path_obj_ids[i]];
                enc_obj.indices_size = static_cast<uint32_t>(enc_verts.size());
                enc_obj.position     = ref_crr;
                enc_obj.visible      = true;
                found = true;
            }

            if (!found)
                scene.objs[line_objs.sc_encounter_path_obj_ids[i]].visible = false;
            return found;
        };

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
                    ss_mut.maneuver_prograde            = ref_pg;
                    ss_mut.maneuver_radial              = ref_rd;
                    ss_mut.maneuver_normal              = ref_nm;
                    ss_mut.maneuver_targets_initialized = true;
                }

                glm::dvec3 const dv = dvWorld(br, bv,
                    ss.maneuver_prograde - ref_pg,
                    ss.maneuver_radial   - ref_rd,
                    ss.maneuver_normal   - ref_nm);
                glm::dvec3 const bv_post = bv + dv;

                if (!ss_mut.spacecraft_states[i].maneuvers.empty() &&
                    !ss_mut.spacecraft_states[i].maneuvers.back().approved)
                {
                    auto& node = ss_mut.spacecraft_states[i].maneuvers.back();
                    node.burn_pos_rel  = br;
                    node.burn_vel_rel  = bv;
                    node.delta_v_world = dv;
                }

                OsculatingOrbit const post_orbit = computeOsculatingOrbit(br, bv_post, ref_GM);
                double const dv_mag = glm::length(dv);
                mo.position          = ref_crr;
                mo.rotation_override = orbitRingMatrix(post_orbit);
                mo.visible           = post_orbit.a > 0.0 && post_orbit.e < 1.0 && dv_mag > 0.0;

                drawEncounterArc(br, post_orbit, dv_mag, ss.maneuver_t0_s);
            }
            else
            {
                // Show approved (non-completed) maneuver orbit using stored burn state
                auto const& sc2 = ss.spacecraft_states[i];
                bool shown = false;
                for (auto const& node : sc2.maneuvers)
                {
                    if (!node.approved || node.completed) continue;
                    if (glm::length(node.delta_v_world) < 1e-12) continue;
                    glm::dvec3 bv_post = node.burn_vel_rel + node.delta_v_world;
                    OsculatingOrbit const aorbit = computeOsculatingOrbit(
                        node.burn_pos_rel, bv_post, ref_GM);
                    if (aorbit.a > 0.0 && aorbit.e < 1.0)
                    {
                        mo.position          = ref_crr;
                        mo.rotation_override = orbitRingMatrix(aorbit);
                        mo.visible           = true;
                        shown = true;
                    }
                    break;
                }
                if (!shown) mo.visible = false;

                // Keep encounter arc visible for approved maneuver, using stored burn state
                if (shown)
                {
                    // sc2 and node are still in scope from the loop above
                    for (auto const& node2 : sc2.maneuvers)
                    {
                        if (!node2.approved || node2.completed) continue;
                        if (glm::length(node2.delta_v_world) < 1e-12) break;
                        glm::dvec3 const bv2_post = node2.burn_vel_rel + node2.delta_v_world;
                        OsculatingOrbit const aorbit2 = computeOsculatingOrbit(
                            node2.burn_pos_rel, bv2_post, ref_GM);
                        double const dt_to_burn =
                            node2.t0_abs_s - ss.elapsed_simulation_s;
                        drawEncounterArc(node2.burn_pos_rel, aorbit2,
                                         glm::length(node2.delta_v_world),
                                         std::max(dt_to_burn, 0.0));
                        break;
                    }
                }
                else if (i < line_objs.sc_encounter_path_obj_ids.size())
                {
                    scene.objs[line_objs.sc_encounter_path_obj_ids[i]].visible = false;
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
                    if (!node.approved || node.completed) continue;
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
