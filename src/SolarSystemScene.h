#pragma once

#include "Scene.h"
#include "Mesh.h"
#include "SolarSystem.h"
#include "VulkanRenderSystem.h"

// Owns the GPU line-geometry buffers for orbit rings and the ecliptic grid.
// Lifetime must exceed the render loop.
struct SolarSystemLineObjects
{
    std::vector<Buffer> orbit_ring_vbufs;
    std::vector<Buffer> orbit_ring_ibufs;
    std::vector<int>    orbit_ring_obj_ids; // indices into Scene::objs
    int                 grid_obj_id{-1};
};

// Add one sphere Object per solar-system body to the scene (program 2).
// Populates PlanetState::scene_object_index for each body.
void initPlanetObjects(Scene& scene, SolarSystem& ss,
                       DrawableMesh const& mesh, Camera const& cam);

// Create orbit-ring and ecliptic-grid line objects (program 3) and return
// the handles needed to update them each frame.
SolarSystemLineObjects initOrbitLines(RenderingState const& state, Scene& scene,
                                      SolarSystem const& ss, Camera const& cam);

// Write PlanetMaterialData into scene.planet_material_buffer for one frame slot.
void writePlanetMaterialBuffers(Scene& scene, SolarSystem const& ss, int frame);

// Update per-frame CRR positions of all planets, orbit rings, and the grid.
void updateSceneFromSolarSystem(Scene& scene, SolarSystem const& ss,
                                SolarSystemLineObjects& line_objs);

// Update the sun light direction from the current camera position.
void updateSunLighting(Scene& scene, Camera const& cam);
