#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/glm.hpp"

#include "Id.h"

#include <vector>
#include <string>
#include <map>

struct ModelBufferObject
{
    alignas(16) glm::mat4 model;
    alignas(16) uint32_t texture_index{0};
};

struct LightBufferObject
{
    alignas(16) glm::vec3 position;
    alignas(16) glm::vec3 light_color;
    float strength;
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
    glm::mat4 proj;
    glm::mat4 view;
    glm::vec3 pos;
    glm::vec3 camera_front;
    glm::vec3 up;
    glm::vec2 pitch_yawn{};
};

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