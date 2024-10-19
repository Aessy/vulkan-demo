#pragma once

#define VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_ASSERT_ON_RESULT
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_funcs.hpp>
#include <vulkan/vulkan_structs.hpp>

#include "descriptor_set.h"

#include "VulkanRenderSystem.h"
#include "TypeLayer.h"
#include "Model.h"
#include "Textures.h"

#include <vector>
#include <variant>
#include <memory>
#include <string>

namespace buffer_types
{

struct BufferBase
{
    std::vector<UniformBuffer> buffers;
};

struct Model : BufferBase
{};

struct World : BufferBase
{};

struct Terrain : BufferBase
{};

struct FogVolume : BufferBase
{};

using ModelType = std::variant<Model, World, Terrain, FogVolume>;

};


struct DescriptionPoolAndSet
{
    vk::DescriptorPool pool;
    std::vector<vk::DescriptorSet> set;

    vk::DescriptorSetLayout layout;
    std::vector<vk::DescriptorSetLayoutBinding> layout_bindings;
};

struct GpuProgram
{
    std::vector<vk::Pipeline> pipeline;
    vk::PipelineLayout pipeline_layout;

    std::vector<DescriptionPoolAndSet> descriptor_sets;
};

struct Program
{
    int id{};
    GpuProgram program;
    std::vector<buffer_types::ModelType> buffers;
};

std::unique_ptr<Program> createProgram(layer_types::Program const& program_data, RenderingState const& core, Textures const& textures, vk::RenderPass const& render_pass);
