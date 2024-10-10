#pragma once

#include "Model.h"
#include "Object.h"
#include "Program.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "glm/gtx/string_cast.hpp"
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <boost/container/flat_map.hpp>

#include <map>
#include <vector>

struct Scene
{
    Camera camera;

    boost::container::flat_map<int, Object> objs;

    std::map<int, std::vector<Object>> objects;

    LightBufferObject light;

    TerrainBufferObject terrain;
};

void addObject(Scene& scene, Object obj)
{
    scene.objs.insert_or_assign(obj.id, obj);

}

inline WorldBufferObject createWorldBufferObject(Scene const& scene)
{
    WorldBufferObject ubo;

    ubo.camera_view = glm::lookAt(scene.camera.pos, (scene.camera.pos + scene.camera.camera_front), scene.camera.up);
    ubo.camera_proj = scene.camera.proj;
    ubo.camera_proj[1][1] *= -1;
    ubo.camera_pos = scene.camera.pos;

    ubo.light_position = scene.light;
    return ubo;
}

inline ModelBufferObject createModelBufferObject(Object const& object)
{
    ModelBufferObject model_buffer{};

    auto rotation = glm::rotate(glm::mat4(1.0f), glm::radians(object.angel), object.rotation);
    auto translation = glm::translate(glm::mat4(1.0f), object.position);
    auto scale = glm::scale(glm::mat4(1.0f), glm::vec3(object.scale, object.scale, object.scale));
    model_buffer.model = translation * rotation * scale;
    model_buffer.texture_index = object.texture_index;

    return model_buffer;
}

inline TerrainBufferObject createTerrainBufferObject(Scene const& scene)
{
    // Should probably be per object, so that eatch object can have unique terrain data.
    return scene.terrain;
}
