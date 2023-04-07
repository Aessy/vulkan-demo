#pragma once

#include "glm/glm.hpp"

#include "Program.h"

#include "Mesh.h"

struct Object
{
    DrawableMesh mesh;
    glm::vec3 position;
    glm::vec3 rotation;
    float scale{1};
    float angel{0};
    int texture_index{0};
};

inline auto createObject(DrawableMesh const& mesh, glm::vec3 const& position = glm::vec3(0,0,0))
{

    Object draw;
    draw.mesh = mesh;
    draw.position = position;
    draw.rotation = glm::vec3(1,1,1);

    return draw;
}