
#include "Model.h"
#include "VulkanRenderSystem.h"
#include "Program.h"

#include "descriptor_set.h"

#include <tuple>
#include <vulkan/vulkan_enums.hpp>

static std::tuple<vk::Pipeline, vk::PipelineLayout> createPipeline(PipelineData const& pipeline_data,
                                                        vk::Extent2D const& swap_chain_extent,
                                                        vk::Device const& device,
                                                        vk::RenderPass const& render_pass,
                                                        vk::SampleCountFlagBits msaa)
{
    std::string path;
    std::vector<vk::PipelineShaderStageCreateInfo> stages;

    vk::ShaderStageFlags shader_flags{};
    for (auto& stage : pipeline_data.shader_stages)
    {
        vk::PipelineShaderStageCreateInfo stage_info;
        stage_info.sType = vk::StructureType::ePipelineShaderStageCreateInfo;
        stage_info.stage = stage.stage;
        stage_info.module = stage.module;
        stage_info.pName = "main";
        stages.push_back(stage_info);

        shader_flags |= stage_info.stage;
    }


    std::vector<vk::DynamicState> dynamic_states {
          vk::DynamicState::eViewport
        , vk::DynamicState::eScissor
    };

    vk::PipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = vk::StructureType::ePipelineDynamicStateCreateInfo;
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state.setDynamicStates(dynamic_states);


    vk::PipelineVertexInputStateCreateInfo vertex_input_info {};

    constexpr auto biding_descrition = getBindingDescription<Vertex>();
    constexpr auto attrib_descriptions = getAttributeDescriptions<Vertex>();

    vertex_input_info.sType = vk::StructureType::ePipelineVertexInputStateCreateInfo;
    vertex_input_info.setVertexBindingDescriptions(biding_descrition);
    vertex_input_info.setVertexAttributeDescriptions(attrib_descriptions);

    vk::PipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = vk::StructureType::ePipelineInputAssemblyStateCreateInfo;
    if (shader_flags & vk::ShaderStageFlagBits::eTessellationControl)
    {
        input_assembly.setTopology(vk::PrimitiveTopology::ePatchList);
    }
    else
    {
        input_assembly.setTopology(vk::PrimitiveTopology::eTriangleList);
    }

    input_assembly.setPrimitiveRestartEnable(false);

    vk::Viewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = (float)swap_chain_extent.width;
    viewport.height= (float)swap_chain_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vk::Rect2D scissor{};
    scissor.offset = vk::Offset2D(0,0);
    scissor.extent = swap_chain_extent;


    vk::PipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = vk::StructureType::ePipelineViewportStateCreateInfo;
    viewport_state.setViewportCount(1);
    viewport_state.setViewports(viewport);
    viewport_state.setScissorCount(1);
    viewport_state.setScissors(scissor);


    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = vk::StructureType::ePipelineMultisampleStateCreateInfo;
    multisampling.sampleShadingEnable = false;
    multisampling.rasterizationSamples = msaa;
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = false;
    multisampling.alphaToOneEnable = false;

    vk::PipelineColorBlendAttachmentState color_blend_attachement{};
    color_blend_attachement.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                             vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

    color_blend_attachement.blendEnable = false;
    color_blend_attachement.srcColorBlendFactor = vk::BlendFactor::eOne;
    color_blend_attachement.dstColorBlendFactor = vk::BlendFactor::eZero;
    color_blend_attachement.colorBlendOp= vk::BlendOp::eAdd;
    color_blend_attachement.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    color_blend_attachement.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    color_blend_attachement.alphaBlendOp = vk::BlendOp::eAdd;

    vk::PipelineColorBlendStateCreateInfo color_blending;
    color_blending.sType = vk::StructureType::ePipelineColorBlendStateCreateInfo;
    color_blending.logicOpEnable = false;
    color_blending.logicOp = vk::LogicOp::eCopy;
    color_blending.attachmentCount = 1;
    color_blending.setAttachments(color_blend_attachement);
    color_blending.blendConstants[0] = 0.0f;
    color_blending.blendConstants[1] = 0.0f;
    color_blending.blendConstants[2] = 0.0f;
    color_blending.blendConstants[3] = 0.0f;

    vk::PipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = vk::StructureType::ePipelineLayoutCreateInfo;
    pipeline_layout_info.setSetLayouts(pipeline_data.descriptor_set_layouts);
    //
    vk::PushConstantRange range;
    range.setStageFlags(shader_flags);
    range.setSize(sizeof(ModelBufferObject));
    range.setOffset(0);

    auto pipeline_layout = device.createPipelineLayout(pipeline_layout_info);

    vk::GraphicsPipelineCreateInfo pipeline_info;
    pipeline_info.sType = vk::StructureType::eGraphicsPipelineCreateInfo;
    pipeline_info.stageCount = stages.size();
    pipeline_info.setStages(stages);

    auto rasterizer_state = createRasterizerState();
    rasterizer_state.setCullMode(vk::CullModeFlagBits::eFront);

    auto depth_stencil_state = createDepthStencil();
    depth_stencil_state.setDepthBoundsTestEnable(false);
    depth_stencil_state.setDepthWriteEnable(false);

    pipeline_info.setPVertexInputState(&vertex_input_info);
    pipeline_info.setPInputAssemblyState(&input_assembly);
    pipeline_info.setPViewportState(&viewport_state);
    pipeline_info.setPRasterizationState(&rasterizer_state);
    pipeline_info.setPMultisampleState(&multisampling);
    pipeline_info.setPDepthStencilState(&depth_stencil_state);
    pipeline_info.setPColorBlendState(&color_blending);
    pipeline_info.setPDynamicState(&dynamic_state);

    pipeline_info.setLayout(pipeline_layout.value);
    pipeline_info.setRenderPass(render_pass);
    pipeline_info.setSubpass(0);
    pipeline_info.basePipelineIndex = 0;
    pipeline_info.basePipelineIndex = -1;

    vk::PipelineTessellationStateCreateInfo tess {};
    if (shader_flags | vk::ShaderStageFlagBits::eTessellationControl)
    {
        tess.sType = vk::StructureType::ePipelineTessellationStateCreateInfo;
        tess.pNext = nullptr;
        tess.patchControlPoints = 3;

        pipeline_info.setPTessellationState(&tess);
    }
    auto pipelines = device.createGraphicsPipelines(VK_NULL_HANDLE, pipeline_info);
    checkResult(pipelines.result);

    vk::Pipeline p = pipelines.value[0];

    return {p, pipeline_layout.value};
}

Pipeline createSkyboxPipeline(RenderingState const& state,
                              vk::RenderPass const& render_pass,
                              std::vector<std::unique_ptr<UniformBuffer>> const& world_buffer,
                              std::vector<std::unique_ptr<UniformBuffer>> const& model_buffer,
                              std::vector<std::unique_ptr<UniformBuffer>> const& atmosphere_data)
{
    layer_types::Program program_desc;
    program_desc.vertex_shader= {{"./shaders/skybox_vert.spv"}};
    program_desc.fragment_shader = {{"./shaders/skybox_frag.spv"}};

    program_desc.buffers.push_back({layer_types::Buffer{
        .name = "World",
        .type = layer_types::BufferType::WorldBufferObject,
        .size = 1,
        .binding = layer_types::Binding{
            .name = {{"binding world"}},
            .binding = 0,
            .type = layer_types::BindingType::Uniform,
            .size = 1,
            .vertex = true,
            .fragment = true
        }
    }});

    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"model_buffer"}},
        .type = layer_types::BufferType::ModelBufferObject,
        .size = 10,
        .binding = layer_types::Binding {
            .name = {{"binding model"}},
            .binding = 0,
            .type = layer_types::BindingType::Storage,
            .size = 1,
            .vertex = true,
            .fragment = true
        }
    }});

    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"model_buffer"}},
        .type = layer_types::BufferType::AtmosphereShaderData,
        .size = 1,
        .binding = layer_types::Binding {
            .name = {{"binding model"}},
            .binding = 0,
            .type = layer_types::BindingType::Uniform,
            .size = 1,
            .vertex = true,
            .fragment = true
        }
    }});

    auto const pipeline_data = createPipelineData(state, program_desc);
    auto const [pipeline, pipeline_layout] = createPipeline(pipeline_data, state.swap_chain.extent, state.device, render_pass, state.msaa);
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
                                           10);

    updateUniformBuffer<Atmosphere>(state.device,
                                    atmosphere_data,
                                    pipeline_finish.descriptor_sets[2].set,
                                    pipeline_finish.descriptor_sets[2].layout_bindings[0],
                                    1);
    return pipeline_finish;
}