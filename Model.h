#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/glm.hpp"

#include <vector>

struct ModelBufferObject
{
    alignas(16) glm::mat4 model;
    alignas(16) uint32_t texture_index;
};

struct LightBufferObject
{
    alignas(16) glm::vec3 position;
};

struct WorldBufferObject
{
    alignas(16) glm::mat4 camera_view;
    alignas(16) glm::mat4 camera_proj;
    alignas(16) glm::vec3 light_position;
};

struct Vertex
{
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 tex_coord;
    glm::vec3 normal;
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
};

Model createBox(uint32_t width, uint32_t height);
