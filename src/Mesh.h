#pragma once

#include "VulkanRenderSystem.h"
#define VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_ASSERT_ON_RESULT
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_structs.hpp>

#include <string>

#include "Id.h"

struct DrawableMesh
{
    std::string name;
    int model_id = -1;

    Buffer vertex_buffer;
    Buffer index_buffer;
    uint32_t indices_size{};
    int id = Id();
};

struct Meshes
{
    std::map<int, DrawableMesh> meshes;

    int loadMesh(RenderingState const& state, Model const& model, std::string const& name = "<noname>")
    {
        DrawableMesh mesh{
            .name = name,
            .model_id = model.id,
            .vertex_buffer = createVertexBuffer(state, model.vertices),
            .index_buffer = createIndexBuffer(state, model.indices)
        };

        meshes.insert({mesh.id, std::move(mesh)});

        return model.id;
    }

    bool loadMesh(RenderingState const& state, std::vector<Vertex> const& vertices, std::vector<uint32_t> const& indices, std::string const& name = "<noname>")
    {
        DrawableMesh mesh{
            .name = name,
            .vertex_buffer = createVertexBuffer(state, vertices),
            .index_buffer = createIndexBuffer(state, indices)
        };

        auto id = mesh.id;

        meshes.insert({mesh.id, std::move(mesh)});
        return id;
}
};

