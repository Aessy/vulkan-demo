#pragma once

#include "Model.h"
#include "Object.h"
#include "Program.h"
#include "VulkanRenderSystem.h"

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

    std::vector<Object> objs;
    std::vector<std::pair<int, int>> obj_inx;
    std::map<int, std::vector<int>> materials;

    std::map<int, std::vector<Object>> objects;

    LightBufferObject light;

    TerrainBufferObject terrain;

    FogVolumeBufferObject fog;
};

inline void addObject(Scene& scene, Object o)
{
    scene.objs.push_back(o);
    int const inx = scene.objs.size()-1;

    auto& material = scene.materials[o.material];
    material.push_back(inx);
    int const material_inx = material.size()-1;

    scene.obj_inx.push_back({o.material, material_inx});
}

inline void changeMaterial(Scene& scene, int obj_ix, int new_material)
{
    auto& obj = scene.objs[obj_ix];
    auto& obj_inx_mapper = scene.obj_inx[obj_ix];

    auto& old_material = scene.materials[obj_inx_mapper.first][obj_inx_mapper.second];
    std::swap(old_material, scene.materials[obj_inx_mapper.first].back());
    scene.materials[obj_inx_mapper.first].pop_back();

    obj.material = new_material;
    auto& material = scene.materials[obj.material];
    material.push_back(obj_ix);

    obj_inx_mapper.first = new_material;
    obj_inx_mapper.second = material.size()-1;
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
    model_buffer.shading_style = object.shading_style;
    if (object.shading_style == 0)
    {
        model_buffer.shininess = object.shininess;
        model_buffer.specular_strength = object.specular_strength;
    }
    else if (object.shading_style == 1)
    {
        model_buffer.metallness = object.metallness;
        model_buffer.roughness = object.roughness;
        model_buffer.ao = object.ao;
    }

    return model_buffer;
}

inline TerrainBufferObject createTerrainBufferObject(Scene const& scene)
{
    // Should probably be per object, so that eatch object can have unique terrain data.
    return scene.terrain;
}
