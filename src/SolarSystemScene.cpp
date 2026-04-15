#include "SolarSystemScene.h"

#include "Object.h"
#include "Material.h"
#include "Pipelines/Planet.h"

#include <numbers>
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

static constexpr double GM_SUN_SCENE = 1.32712440018e11; // km^3 / s^2

static KeplerOrbit computeOsculatingOrbit(glm::dvec3 r, glm::dvec3 v)
{
    double const r_mag  = glm::length(r);
    double const v_sq   = glm::dot(v, v);
    double const energy = v_sq / 2.0 - GM_SUN_SCENE / r_mag;
    double const a      = -GM_SUN_SCENE / (2.0 * energy);

    glm::dvec3 const h      = glm::cross(r, v);
    double     const h_mag  = glm::length(h);
    glm::dvec3 const e_vec  = glm::cross(v, h) / GM_SUN_SCENE - r / r_mag;
    double     const e      = glm::length(e_vec);

    glm::dvec3 const e_hat = (e > 1e-10) ? e_vec / e : glm::dvec3(1.0, 0.0, 0.0);
    glm::dvec3 const n_hat = h / h_mag;
    glm::dvec3 const q_hat = glm::cross(n_hat, e_hat); // 90° ahead in orbit

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
            ss.states[i].position_km, ss.states[i].velocity_km);

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

        writeBuffer(*scene.planet_material_buffer[frame], mat, base_index + i);
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
}

void updateSunLighting(Scene& scene, Camera const& cam)
{
    glm::vec3 const sun_cam_rel = glm::vec3(-cam.pos_d);
    scene.light.position = sun_cam_rel;
    scene.light.sun_pos  = glm::length(sun_cam_rel) > 0.0f
                           ? glm::normalize(sun_cam_rel)
                           : glm::vec3(1.0f, 0.0f, 0.0f);
}
