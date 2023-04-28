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
    alignas(16) uint32_t texture_index;
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

struct TerrainBufferObject
{
    float max_height{0};
    int displacement_map{};
    int normal_map{2};
    int texture_id{8};
    int texture_normal_map{7};
    
    float blend_sharpness{6};

    float texture_scale{0.06};


    float lod_min{0};
    float lod_max{8};
    float weight{386};
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
};

Model createBox();