#pragma once

#include "Material.h"
#include "Model.h"
#include "Object.h"
#include "Program.h"
#include "VulkanRenderSystem.h"
#include <variant>

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
    std::map<int, std::vector<int>> programs;

    std::map<int, std::vector<Object>> objects;

    LightBufferObject light;

    TerrainBufferObject terrain;

    FogVolumeBufferObject fog;
};

inline void addObject(Scene& scene, Object o)
{
    scene.objs.push_back(o);
    int const inx = scene.objs.size()-1;

    auto& material = scene.programs[o.material.program];
    material.push_back(inx);
    int const material_inx = material.size()-1;

    scene.obj_inx.push_back({o.material.program, material_inx});
}

inline void changeMaterial(Scene& scene, int obj_ix, int new_material)
{
    auto& obj = scene.objs[obj_ix];
    auto& obj_inx_mapper = scene.obj_inx[obj_ix];

    auto& old_material = scene.programs[obj_inx_mapper.first][obj_inx_mapper.second];
    std::swap(old_material, scene.programs[obj_inx_mapper.first].back());
    scene.programs[obj_inx_mapper.first].pop_back();

    obj.material.program = new_material;
    auto& material = scene.programs[obj.material.program];
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

    auto& material = object.material;

    model_buffer.texture_index = material.base_color_texture;
    model_buffer.shading_style = material.mode;
    if (material.mode == ReflectionShadeMode::Phong)
    {
        model_buffer.shininess = material.shininess;
        model_buffer.specular_strength = material.specular_strength;
    }
    else if (material.mode == ReflectionShadeMode::PBR)
    {
        model_buffer.metallness = material.metallic;
        model_buffer.roughness = material.roughness;
        model_buffer.ao = material.ao;
        model_buffer.texture_normal = material.base_color_normal_texture;
        model_buffer.texture_roughness = material.roughness_texture;
        model_buffer.texture_ao = material.ao_texture;
    }

    return model_buffer;
}

inline TerrainBufferObject createTerrainBufferObject(Object const& object)
{
    auto& material = object.material;
    TerrainBufferObject tbo{};

    tbo.displacement_map = material.displacement_map_texture,
    tbo.normal_map = material.normal_map_texture;
    tbo.max_height = material.displacement_y;

    tbo.texture_id = material.base_color_texture;
    tbo.texture_normal_map = material.base_color_texture;
    tbo.metalness = material.metallic;
    tbo.roughness = material.roughness;
    tbo.ao = material.ao;

    if (material.has_rougness_tex) {
        tbo.roughness_texture_id = material.roughness_texture;
    }
    if (material.has_metallic_tex) {
        tbo.metallic_texture_id = material.metallic_texture;
    }
    if (material.has_ao_tex) {
        tbo.ao_texture_id = material.ao_texture;
    }

    tbo.texture_scale = material.scaling_factor;

    return tbo;
}
