#pragma once

#define VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_ASSERT_ON_RESULT
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_structs.hpp>

#include <string>

struct DrawableMesh
{
    std::string name;

    vk::Buffer vertex_buffer;
    vk::Buffer index_buffer;
    uint32_t indices_size{};
};

inline DrawableMesh loadMesh(RenderingState const& state, Model const& model, std::string const& name = "<noname>")
{
    DrawableMesh mesh;
    mesh.vertex_buffer = createVertexBuffer(state, model.vertices);
    mesh.index_buffer = createIndexBuffer(state, model.indices);
    mesh.indices_size = model.indices.size();
    mesh.name = name;

    return mesh;
}

inline DrawableMesh loadMesh(RenderingState const& state, std::vector<Vertex> const& vertices, std::vector<uint32_t> const& indices, std::string const& name = "<noname>")
{
    DrawableMesh mesh;
    mesh.vertex_buffer = createVertexBuffer(state, vertices);
    mesh.index_buffer = createIndexBuffer(state, indices);
    mesh.indices_size = indices.size();
    mesh.name = name;

    return mesh;
}