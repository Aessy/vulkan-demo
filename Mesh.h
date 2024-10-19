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

    vk::Buffer vertex_buffer;
    vk::Buffer index_buffer;
    uint32_t indices_size{};
    int id = Id();
};

inline void test()
{
    DrawableMesh m;

    auto m2 = m;
}

struct Meshes
{
    std::map<int, DrawableMesh> meshes;

    int loadMesh(RenderingState const& state, Model const& model, std::string const& name = "<noname>")
    {
        DrawableMesh mesh{};

        mesh.model_id = model.id;
        mesh.vertex_buffer = createVertexBuffer(state, model.vertices);
        mesh.index_buffer = createIndexBuffer(state, model.indices);
        mesh.indices_size = model.indices.size();
        mesh.name = name;

        meshes.insert({mesh.id, mesh});

        return mesh.id;
    }

    bool loadMesh(RenderingState const& state, std::vector<Vertex> const& vertices, std::vector<uint32_t> const& indices, std::string const& name = "<noname>")
    {
        DrawableMesh mesh{};
        mesh.vertex_buffer = createVertexBuffer(state, vertices);
        mesh.index_buffer = createIndexBuffer(state, indices);
        mesh.indices_size = indices.size();
        mesh.name = name;

        meshes.insert({mesh.id, mesh});
        return mesh.id;
}
};

