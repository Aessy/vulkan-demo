#pragma once

#include "Material.h"

#include "Id.h"
#include "Mesh.h"

/*
struct Terrain
{
    size_t max_height{};
    int displacement_map{-1};
};
*/

enum class ObjectType
{
    STANDARD,
    SKYBOX
};

struct Object
{
    // Weak handles to the buffer for now. Fix later.
    vk::Buffer vertex_buffer;
    vk::Buffer index_buffer;
    uint32_t indices_size;

    glm::vec3 position;
    glm::vec3 rotation;

    float scale{1};
    float angel{0};
    Material material;

    std::optional<Lod> lod;

    bool shadow = false;

    ObjectType object_type = ObjectType::STANDARD;

    int id = Id();
};


inline auto createObject(DrawableMesh const& mesh, glm::vec3 const& position = glm::vec3(0,0,0))
{
    Object draw{};

    draw.vertex_buffer = mesh.vertex_buffer.buffer;
    draw.index_buffer = mesh.index_buffer.buffer;
    draw.indices_size = mesh.indices_size;

    draw.position = position;
    draw.rotation = glm::vec3(1,1,1);

    return draw;
}
