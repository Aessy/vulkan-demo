#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/glm.hpp"

#include "Id.h"

#include <vector>
#include <string>
#include <map>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "glm/gtx/string_cast.hpp"
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

struct ModelBufferObject
{
    alignas(16) glm::mat4 model;
    alignas(16) uint32_t texture_index{0};
};

struct LightBufferObject
{
    alignas(16) glm::vec3 position;
    alignas(16) glm::vec3 light_color;
    alignas(16) glm::vec3 sun_pos;
    float strength;
    float time_of_the_day{0.5};
};

struct WorldBufferObject
{
    alignas(16) glm::mat4 camera_view;
    alignas(16) glm::mat4 camera_proj;
    alignas(16) glm::vec3 camera_pos;
    alignas(16) LightBufferObject light_position;
};

struct FogVolumeBufferObject
{
    unsigned int volumetric_fog_enabled {0};
    float base_density = 0.00f;
    float max_density = 0.9f;
    alignas(16) glm::vec3 color = glm::vec3(1,1,1);
    float turbulence;
    float wind;
    float time;
};

struct Atmosphere
{
    float sun_distance{450000};
    float mie_coefficient{0.005};
    float mie_scattering_dir{0.758};
    float rayleigh_scatter{2};

    float turbidity{10};
    float luminance{1};
    float sun_exposure{1000.0};
};

struct TerrainBufferObject
{
    float max_height{153.6};
    int displacement_map{19};
    int normal_map{20};
    int texture_id{21};
    int roughness_texture_id{-1};
    int metallic_texture_id{-1};
    int ao_texture_id{-1};
    int texture_normal_map{22};
    float blend_sharpness{20};

    float shininess{0.5f};
    float specular_strength{0.5f};

    float metalness{0.0f};
    float roughness{0.5f};
    float ao{0.5f};

    float texture_scale{0.020};


    float lod_min{0};
    float lod_max{8};
    float weight{300};
};

struct CascadedShadowMapBufferObject
{
    glm::mat4 light_projection_view;
};

struct PostProcessingBufferObject
{
    int hdr_resolve{0};
};

struct Vertex
{
    glm::vec3 pos;
    glm::vec2 tex_coord;
    glm::vec3 normal;
    glm::vec2 normal_coord;
    glm::vec3 tangent;
    glm::vec3 bitangent;
};

struct Camera
{
    glm::mat4  proj;
    glm::mat4  view;
    glm::vec3  pos;                       // always vec3(0) in CRR
    glm::vec3  camera_front{0.0f, 0.0f, -1.0f};  // derived, toward orbit_target
    glm::vec3  up{0.0f, 1.0f, 0.0f};     // derived
    glm::dvec3 pos_d{0.0};               // derived — true world position in km

    // Orbit camera parameters (authoritative state):
    glm::dvec3 orbit_target{0.0};        // point to orbit around (km); Sun = origin
    double     orbit_distance{3e8};      // km from target
    float      orbit_azimuth{20.0f};     // horizontal angle around target (degrees)
    float      orbit_elevation{30.0f};   // angle above ecliptic (degrees); clamped [-89, 89]
};

// Derive pos_d, camera_front, and up from orbit parameters.
// Call after any orbit parameter change.
inline void updateCameraFromOrbit(Camera& cam)
{
    float az  = glm::radians(cam.orbit_azimuth);
    float el  = glm::radians(cam.orbit_elevation);
    float cel = std::cos(el);

    glm::dvec3 offset{
        (double)(std::sin(az) * cel) * cam.orbit_distance,
        (double) std::sin(el)        * cam.orbit_distance,
        (double)(std::cos(az) * cel) * cam.orbit_distance
    };

    cam.pos_d        = cam.orbit_target + offset;
    cam.pos          = glm::vec3(0.0f);
    cam.camera_front = glm::normalize(glm::vec3(-offset));
    cam.up           = glm::vec3(0.0f, 1.0f, 0.0f);
}

struct Model
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    std::string path;
    int const id = Id();
};

struct Models
{
    std::map<int, Model> models;
    int loadModel(std::string const& model_path);
    int loadModelAssimp(std::string const& path);
};

Model createBox();