#pragma once


#include "VulkanRenderSystem.h"
#include "TypeLayer.h"
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

struct PostProcessingData : BufferBase
{};

struct MaterialShaderData : BufferBase
{};

struct AtmosphereShaderData : BufferBase
{};

struct CascadedShadowMapBufferObject: BufferBase
{};

using ModelType = std::variant<Model, World, Terrain, FogVolume, PostProcessingData, MaterialShaderData, AtmosphereShaderData, CascadedShadowMapBufferObject>;

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
    std::string name{};
    GpuProgram program;
    std::vector<buffer_types::ModelType> buffers;
};

std::unique_ptr<Program> createProgram(layer_types::Program const& program_data,
                                       RenderingState const& core,
                                       Textures const& textures,
                                       vk::RenderPass const& render_pass,
                                       std::string const& name,
                                       GraphicsPipelineInput const& input);

struct PipelineData
{
    std::vector<std::vector<vk::DescriptorSetLayoutBinding>> descriptor_set_layout_bindings;
    std::vector<ShaderStage> shader_stages;
    std::vector<DescriptionPoolAndSet> descriptor_sets;
    std::vector<vk::DescriptorSetLayout> descriptor_set_layouts;

    layer_types::Program const& program_data;
};

PipelineData createPipelineData(RenderingState const& state, layer_types::Program const& program_data);

struct Pipeline
{
    vk::Pipeline pipeline;
    vk::PipelineLayout pipeline_layout;

    std::vector<vk::DescriptorSetLayout> descriptor_set_layouts;
    std::vector<std::vector<vk::DescriptorSetLayoutBinding>> bindings;
    std::vector<DescriptionPoolAndSet> descriptor_sets;
};

Pipeline bindPipeline(PipelineData const& pipeline_data, vk::Pipeline const& pipeline, vk::PipelineLayout const& pipeline_layout);
