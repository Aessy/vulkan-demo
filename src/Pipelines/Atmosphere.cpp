#include "Atmosphere.h"
#include "Pipeline.h"
#include "Model.h"
#include "VulkanRenderSystem.h"
#include "Program.h"
#include "descriptor_set.h"

Pipeline createAtmospherePipeline(RenderingState const& state,
                                  vk::RenderPass const& render_pass,
                                  std::vector<std::unique_ptr<UniformBuffer>> const& world_buffer,
                                  std::vector<std::unique_ptr<UniformBuffer>> const& model_buffer,
                                  std::vector<std::unique_ptr<UniformBuffer>> const& atmosphere_color_buffer)
{
    layer_types::Program program_desc;
    program_desc.vertex_shader   = {{"./shaders/atmosphere_vert.spv"}};
    program_desc.fragment_shader = {{"./shaders/atmosphere_frag.spv"}};

    // Set 0: WorldBufferObject
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"world_buffer"}},
        .type = layer_types::BufferType::WorldBufferObject,
        .size = 1,
        .binding = layer_types::Binding{
            .name     = {{"binding world"}},
            .binding  = 0,
            .type     = layer_types::BindingType::Uniform,
            .size     = 1,
            .vertex   = true,
            .fragment = false,
        }
    }});

    // Set 1: ModelBufferObject SSBO
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"model_buffer"}},
        .type = layer_types::BufferType::ModelBufferObject,
        .size = 20,
        .binding = layer_types::Binding{
            .name     = {{"binding model"}},
            .binding  = 0,
            .type     = layer_types::BindingType::Storage,
            .size     = 1,
            .vertex   = true,
            .fragment = false,
        }
    }});

    // Set 2: atmosphere color+scale SSBO (vec4 per planet)
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"atmosphere_buffer"}},
        .type = layer_types::BufferType::NoBuffer,
        .size = 20,
        .binding = layer_types::Binding{
            .name     = {{"binding atmosphere"}},
            .binding  = 0,
            .type     = layer_types::BindingType::Storage,
            .size     = 1,
            .vertex   = false,
            .fragment = true,
        }
    }});

    auto const pipeline_data = createPipelineData(state, program_desc);

    GraphicsPipelineInput input = createDefaultPipelineInput();
    input.rasterizer_state.cullMode      = vk::CullModeFlagBits::eBack;
    input.depth_stencil.depthTestEnable  = true;
    input.depth_stencil.depthWriteEnable = false;
    input.depth_stencil.depthCompareOp   = vk::CompareOp::eLessOrEqual;
    input.blend_attachment.blendEnable         = true;
    input.blend_attachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    input.blend_attachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    input.blend_attachment.colorBlendOp        = vk::BlendOp::eAdd;
    input.blend_attachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    input.blend_attachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    input.blend_attachment.alphaBlendOp        = vk::BlendOp::eAdd;
    input.blend_attachment.colorWriteMask      = vk::ColorComponentFlagBits::eR |
                                                 vk::ColorComponentFlagBits::eG |
                                                 vk::ColorComponentFlagBits::eB |
                                                 vk::ColorComponentFlagBits::eA;

    auto const [pipeline, pipeline_layout] = createPipeline(pipeline_data,
                                                            state.swap_chain.extent,
                                                            state.device,
                                                            render_pass,
                                                            state.msaa,
                                                            input);
    auto pipeline_finish = bindPipeline(pipeline_data, pipeline, pipeline_layout);

    updateUniformBuffer<WorldBufferObject>(state.device,
                                          world_buffer,
                                          pipeline_finish.descriptor_sets[0].set,
                                          pipeline_finish.descriptor_sets[0].layout_bindings[0],
                                          1);

    updateUniformBuffer<ModelBufferObject>(state.device,
                                          model_buffer,
                                          pipeline_finish.descriptor_sets[1].set,
                                          pipeline_finish.descriptor_sets[1].layout_bindings[0],
                                          20);

    updateUniformBuffer<glm::vec4>(state.device,
                                   atmosphere_color_buffer,
                                   pipeline_finish.descriptor_sets[2].set,
                                   pipeline_finish.descriptor_sets[2].layout_bindings[0],
                                   20);

    return pipeline_finish;
}
