#pragma once

#include "Material.h"
#include "glm/glm.hpp"

#include "Program.h"

#include "Id.h"
#include "Mesh.h"

/*
struct Terrain
{
    size_t max_height{};
    int displacement_map{-1};
};
*/

struct Object
{
    DrawableMesh mesh;
    glm::vec3 position;
    glm::vec3 rotation;

    float scale{1};
    float angel{0};
    Material material;

    std::optional<Lod> lod;

    int id = Id();
};


inline auto createObject(DrawableMesh const& mesh, glm::vec3 const& position = glm::vec3(0,0,0))
{
    Object draw;
    draw.mesh = mesh;
    draw.position = position;
    draw.rotation = glm::vec3(1,1,1);

    return draw;
}
