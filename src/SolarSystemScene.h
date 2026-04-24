#pragma once

#include "Scene.h"
#include "Mesh.h"
#include "SolarSystem.h"
#include "VulkanRenderSystem.h"
#include <optional>

// Owns the GPU line-geometry buffers for orbit rings and the ecliptic grid.
// Lifetime must exceed the render loop.
struct SolarSystemLineObjects
{
    std::vector<Buffer> orbit_ring_vbufs;
    std::vector<Buffer> orbit_ring_ibufs;
    std::vector<int>    orbit_ring_obj_ids; // indices into Scene::objs
    int                 grid_obj_id{-1};

    // Per-spacecraft osculating orbit rings (around Earth)
    std::vector<Buffer> sc_orbit_vbufs;
    std::vector<Buffer> sc_orbit_ibufs;
    std::vector<int>    sc_orbit_obj_ids;

    // Per-spacecraft predicted N-body path (pre-allocated, updated in-place)
    static constexpr int MAX_PATH_VERTS = 4000; // 2000 segments × 2 verts
    std::vector<Buffer> sc_path_vbufs;
    std::vector<Buffer> sc_path_ibufs;
    std::vector<int>    sc_path_obj_ids;

    // Per-spacecraft: post-burn predicted orbit ring (Kepler, different color)
    std::vector<Buffer> sc_maneuver_orbit_vbufs;
    std::vector<Buffer> sc_maneuver_orbit_ibufs;
    std::vector<int>    sc_maneuver_orbit_obj_ids;

    // Single burn node marker (small cross shown on current orbit ring)
    std::optional<Buffer> maneuver_node_vbuf;
    std::optional<Buffer> maneuver_node_ibuf;
    int    maneuver_node_obj_id{-1};

    // Ghost spacecraft shown at burn position
    int    maneuver_ghost_obj_id{-1};
};

// Add one sphere Object per solar-system body to the scene (program 2).
// Populates PlanetState::scene_object_index for each body.
void initPlanetObjects(Scene& scene, SolarSystem& ss,
                       DrawableMesh const& mesh, Camera const& cam);

// Add one sphere Object per moon to the scene (program 2).
// Populates MoonState::scene_object_index for each moon.
void initMoonObjects(Scene& scene, SolarSystem& ss,
                     DrawableMesh const& mesh, Camera const& cam);

// Create orbit-ring and ecliptic-grid line objects (program 3) and return
// the handles needed to update them each frame.
SolarSystemLineObjects initOrbitLines(RenderingState const& state, Scene& scene,
                                      SolarSystem const& ss, Camera const& cam);

// Write PlanetMaterialData into scene.planet_material_buffer for one frame slot.
void writePlanetMaterialBuffers(Scene& scene, SolarSystem const& ss, int frame);

// Write PlanetMaterialData for moons into planet_material_buffer (after planets).
void writeMoonMaterialBuffers(Scene& scene, SolarSystem const& ss, int frame);

// Write atmosphere color+scale (vec4) into scene.atmosphere_color_buffer for one frame slot.
void writeAtmosphereColorBuffers(Scene& scene, SolarSystem const& ss, int frame);

// Update per-frame CRR positions of all planets, orbit rings, and the grid.
void updateSceneFromSolarSystem(Scene& scene, SolarSystem const& ss,
                                SolarSystemLineObjects& line_objs);

// Update the sun light direction from the current camera position.
void updateSunLighting(Scene& scene, Camera const& cam);

// Add one Object per spacecraft to the scene (program 2, same as planets).
// Populates SpacecraftState::scene_object_index for each craft.
// Also adds the maneuver ghost object and stores its id in line_objs.
void initSpacecraftObjects(Scene& scene, SolarSystem& ss,
                           DrawableMesh const& mesh, Camera const& cam,
                           SolarSystemLineObjects& line_objs);

// Write PlanetMaterialData for spacecraft (and ghost) into planet_material_buffer.
void writeSpacecraftMaterialBuffers(Scene& scene, SolarSystem const& ss,
                                    SolarSystemLineObjects const& line_objs, int frame);

// Create osculating orbit ring + predicted path line objects for all current spacecraft.
// Call after initOrbitLines() and whenever spacecraft are spawned.
void initSpacecraftLines(RenderingState const& state, Scene& scene,
                         SolarSystem const& ss, Camera const& cam,
                         SolarSystemLineObjects& line_objs);
