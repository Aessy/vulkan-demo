#include "SolarSystemScene.h"

#include "Object.h"
#include "Material.h"
#include "Pipelines/Planet.h"
#include "Spacecraft.h"

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

// Osculating Keplerian orbit derived from a state vector (position + velocity).
// Vertices will be in km, centred on the Sun (focus), in the correct 3D orbital plane.
struct KeplerOrbit {
    double      a;      // semi-major axis (km)
    double      e;      // eccentricity
    glm::dvec3  e_hat;  // unit vector toward periapsis
    glm::dvec3  q_hat;  // unit vector 90° ahead of periapsis in orbital plane
};

static constexpr double GM_SUN_SCENE   = 1.32712440018e11; // km^3 / s^2
static constexpr double GM_EARTH_SCENE = 3.986e5;          // km^3 / s^2

static KeplerOrbit computeOsculatingOrbit(glm::dvec3 r, glm::dvec3 v, double GM)
{
    double const r_mag  = glm::length(r);
    double const v_sq   = glm::dot(v, v);
    double const energy = v_sq / 2.0 - GM / r_mag;
    double const a      = -GM / (2.0 * energy);

    glm::dvec3 const h      = glm::cross(r, v);
    double     const h_mag  = glm::length(h);
    glm::dvec3 const e_vec  = glm::cross(v, h) / GM - r / r_mag;
    double     const e      = glm::length(e_vec);

    glm::dvec3 const e_hat = (e > 1e-10) ? e_vec / e : glm::dvec3(1.0, 0.0, 0.0);
    glm::dvec3 const n_hat = h / h_mag;
    glm::dvec3 const q_hat = glm::cross(n_hat, e_hat);

    return { a, e, e_hat, q_hat };
}

// Keplerian ellipse in 3D, vertices in km relative to Sun (focus). N LINE_LIST segments.
static std::pair<std::vector<LineVertex>, std::vector<uint32_t>>
makeOrbitEllipseGeometry(KeplerOrbit const& orbit, glm::vec4 color, int N = 256)
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
        KeplerOrbit const orbit = computeOsculatingOrbit(
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
// Returns positions in km relative to Earth at each recorded step.
static std::vector<glm::dvec3> buildSpacecraftPath(SolarSystem const& ss,
                                                    int sc_idx, double duration_s)
{
    SolarSystem copy = ss;
    copy.time_scale  = 1.0;
    for (auto& sc : copy.spacecraft_states)
        sc.thrust_level = 0.0;

    constexpr std::size_t earth_idx  = 3;
    double const          earth_r    = copy.defs[earth_idx].radius_km;
    constexpr double      step_s     = 30.0;
    int const             steps      = static_cast<int>(duration_s / step_s);

    std::vector<glm::dvec3> positions;
    positions.reserve(steps + 1);

    // Record current position first so the path starts at the spacecraft.
    {
        glm::dvec3 ep0 = interpolatedPosition(copy, earth_idx);
        positions.push_back(copy.spacecraft_states[sc_idx].position_km - ep0);
    }

    for (int i = 0; i < steps; ++i)
    {
        updateSolarSystem(copy, step_s);
        glm::dvec3 earth_pos = interpolatedPosition(copy, earth_idx);
        glm::dvec3 sc_pos    = copy.spacecraft_states[sc_idx].position_km;
        glm::dvec3 rel       = sc_pos - earth_pos;

        double dist = glm::length(rel);
        positions.push_back(rel);

        if (dist < earth_r) break;              // crashed
        if (dist > 2e6)     break;              // escaped Earth sphere of influence
    }
    return positions;
}

void initSpacecraftLines(RenderingState const& state, Scene& scene,
                         SolarSystem const& ss, Camera const& cam,
                         SolarSystemLineObjects& line_objs)
{
    Material const lines_mat{.name = {"Lines"}, .program = 3, .shader_data = {}};
    constexpr std::size_t earth_idx = 3;

    glm::dvec3 earth_vel = ss.states[earth_idx].velocity_km;

    for (std::size_t i = 0; i < ss.spacecraft_states.size(); ++i)
    {
        auto const& sc = ss.spacecraft_states[i];

        // --- Orbit ring ---
        glm::dvec3 earth_pos_phys   = planetPositionAtSpacecraftTime(ss, earth_idx, i);
        glm::dvec3 earth_pos_render = interpolatedPosition(ss, earth_idx);
        glm::dvec3 r_rel = sc.position_km - earth_pos_phys;
        glm::dvec3 v_rel = sc.velocity_km  - earth_vel;
        KeplerOrbit orbit = computeOsculatingOrbit(r_rel, v_rel, GM_EARTH_SCENE);

        auto [ring_v, ring_i] = makeOrbitEllipseGeometry(
            orbit, glm::vec4(1.0f, 0.9f, 0.3f, 1.0f));

        auto ov = createLineVertexBuffer(state, ring_v);
        auto oi = createIndexBuffer(state, ring_i);

        Object ro{};
        ro.vertex_buffer = ov.buffer;
        ro.index_buffer  = oi.buffer;
        ro.indices_size  = static_cast<uint32_t>(ring_i.size());
        ro.position      = glm::vec3(earth_pos_render - cam.pos_d);
        ro.scale         = 1.0f;
        ro.rotation      = glm::vec3(0.0f, 1.0f, 0.0f);
        ro.material      = lines_mat;
        ro.line_width    = ss.spacecraft_orbit_line_width;
        ro.line_alpha    = ss.spacecraft_orbit_opacity;
        ro.visible       = ss.show_spacecraft_orbit && orbit.e < 1.0;

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

    constexpr std::size_t earth_idx = 3;
    glm::dvec3 const earth_vel = ss.states[earth_idx].velocity_km;

    for (std::size_t i = 0; i < ss.spacecraft_states.size(); ++i)
    {
        if (i >= line_objs.sc_orbit_obj_ids.size()) break;
        auto const& sc = ss.spacecraft_states[i];

        // Physics-time Earth position: consistent with sc.position_km for orbit shape.
        // Render-time Earth position: where the ring object is anchored in the scene.
        glm::dvec3 const earth_pos_phys = planetPositionAtSpacecraftTime(ss, earth_idx, i);
        glm::dvec3 const earth_pos_render = interpolatedPosition(ss, earth_idx);
        glm::vec3  const earth_crr = glm::vec3(earth_pos_render - scene.camera.pos_d);

        // --- Osculating orbit ring ---
        glm::dvec3 r_rel = sc.position_km - earth_pos_phys;
        glm::dvec3 v_rel = sc.velocity_km  - earth_vel;
        KeplerOrbit orbit = computeOsculatingOrbit(r_rel, v_rel, GM_EARTH_SCENE);

        auto& ring_obj      = scene.objs[line_objs.sc_orbit_obj_ids[i]];
        ring_obj.position   = earth_crr;
        ring_obj.line_width = ss.spacecraft_orbit_line_width;
        ring_obj.line_alpha = ss.spacecraft_orbit_opacity;
        ring_obj.visible    = ss.show_spacecraft_orbit && orbit.e < 1.0 && orbit.a > 0.0;

        if (ring_obj.visible)
        {
            auto [rv, ri] = makeOrbitEllipseGeometry(orbit, glm::vec4(1.0f, 0.9f, 0.3f, 1.0f));
            auto& vbuf    = line_objs.sc_orbit_vbufs[i];
            vk::DeviceSize sz = sizeof(LineVertex) * rv.size();
            void* ptr = vbuf.memory.mapMemory(0, sz).value;
            std::memcpy(ptr, rv.data(), static_cast<std::size_t>(sz));
            vbuf.memory.unmapMemory();
        }

        // --- Predicted N-body path ---
        auto& path_obj = scene.objs[line_objs.sc_path_obj_ids[i]];
        path_obj.position = earth_crr;

        bool path_active = ss.show_spacecraft_path;
        path_obj.visible  = path_active;

        if (path_active && ss.spacecraft_path_dirty)
        {
            auto pts = buildSpacecraftPath(ss, static_cast<int>(i),
                                           ss.spacecraft_path_duration_s);

            // Indices are pre-allocated as 0,1,2,3,... (sequential pairs for LINE_LIST).
            // Only the vertex buffer (host-visible) needs updating.
            int const max_segs = (SolarSystemLineObjects::MAX_PATH_VERTS / 2) - 1;
            int const n        = std::min(static_cast<int>(pts.size()) - 1, max_segs);

            std::vector<LineVertex> pv;
            pv.reserve(n * 2);

            for (int s = 0; s < n; ++s)
            {
                float t   = static_cast<float>(s) / static_cast<float>(std::max(n - 1, 1));
                glm::vec4 col = glm::mix(glm::vec4(1.0f, 0.9f, 0.3f, 0.9f),
                                         glm::vec4(0.5f, 0.45f, 0.15f, 0.15f), t);
                pv.push_back({glm::vec3(pts[s]),     col, t});
                pv.push_back({glm::vec3(pts[s + 1]), col, t});
            }

            path_obj.indices_size = static_cast<uint32_t>(pv.size()); // sequential indices

            if (!pv.empty())
            {
                auto& pvbuf = line_objs.sc_path_vbufs[i];
                vk::DeviceSize vsz = sizeof(LineVertex) * pv.size();
                void* vptr = pvbuf.memory.mapMemory(0, vsz).value;
                std::memcpy(vptr, pv.data(), static_cast<std::size_t>(vsz));
                pvbuf.memory.unmapMemory();
            }

            const_cast<SolarSystem&>(ss).spacecraft_path_dirty = false;
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
                           DrawableMesh const& mesh, Camera const& cam)
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
}

void writeSpacecraftMaterialBuffers(Scene& scene, SolarSystem const& ss, int frame)
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
        mat.emissive               = 1.0f; // always fully lit — no sun shading on spacecraft
        mat.cloud_texture          = -1;

        writeBuffer(*scene.planet_material_buffer[frame], mat,
                    base_index + planet_count + moon_count + j);
    }
}
