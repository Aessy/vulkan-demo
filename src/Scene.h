#pragma once

#include "Material.h"
#include "Model.h"
#include "Object.h"
#include "VulkanRenderSystem.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <map>
#include <vector>

struct Scene
{
    Camera camera;

    std::vector<Object> objs;
    std::vector<std::pair<int, int>> obj_inx;
    std::map<int, std::vector<int>> programs;

    LightBufferObject light;
    TerrainBufferObject terrain;
    FogVolumeBufferObject fog;
    Atmosphere atmosphere;

    std::vector<std::unique_ptr<UniformBuffer>> world_buffer;
    std::vector<std::unique_ptr<UniformBuffer>> model_buffer;
    std::vector<std::unique_ptr<UniformBuffer>> material_buffer;
    std::vector<std::unique_ptr<UniformBuffer>> atmosphere_data;
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
    //ubo.camera_proj[1][1] *= -1;
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
    return model_buffer;
}

inline void sceneWriteBuffers(Scene & scene, uint32_t frame)
{
    WorldBufferObject ubo = createWorldBufferObject(scene);
    writeBuffer(*scene.world_buffer[frame], ubo);
    writeBuffer(*scene.atmosphere_data[frame], scene.atmosphere);

    // Need to add material and model matrix data in the buffers the same order
    // the objects will be rendered.
    int index = 0;
    for (auto & o : scene.programs)
    {
        for (int i = 0; i < o.second.size(); ++i)
        {
            auto &obj = scene.objs[o.second[i]];

            if (obj.object_type == ObjectType::SKYBOX)
            {
                obj.position = scene.camera.pos;
            }

            auto ubo = createModelBufferObject(obj);
            writeBuffer(*scene.model_buffer[frame], ubo, index);
            writeBuffer(*scene.material_buffer[frame], obj.material.shader_data, index);

            ++index;
        }
    }
}