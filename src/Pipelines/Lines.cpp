#include "Lines.h"
#include "Model.h"
#include "VulkanRenderSystem.h"
#include "Program.h"
#include "TypeLayer.h"
#include "descriptor_set.h"

#include <array>

Pipeline createLinesPipeline(RenderingState const& state,
                             vk::RenderPass const& render_pass,
                             std::vector<std::unique_ptr<UniformBuffer>> const& world_buffer,
                             std::vector<std::unique_ptr<UniformBuffer>> const& model_buffer)
{
    layer_types::Program program_desc;
    program_desc.vertex_shader   = {{"./shaders/lines_vert.spv"}};
    program_desc.fragment_shader = {{"./shaders/lines_frag.spv"}};

    // Set 0: WorldBufferObject (uniform) — vertex stage only
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

    // Set 1: ModelBufferObject SSBO (size 20) — vertex stage
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

    auto const pipeline_data = createPipelineData(state, program_desc);

    // --- Vertex input for LineVertex ---
    vk::VertexInputBindingDescription binding_desc{};
    binding_desc.binding   = 0;
    binding_desc.stride    = sizeof(LineVertex);
    binding_desc.inputRate = vk::VertexInputRate::eVertex;

    std::array<vk::VertexInputAttributeDescription, 3> attrib_descs{};
    attrib_descs[0].binding  = 0;
    attrib_descs[0].location = 0;
    attrib_descs[0].format   = vk::Format::eR32G32B32Sfloat;
    attrib_descs[0].offset   = offsetof(LineVertex, pos);
    attrib_descs[1].binding  = 0;
    attrib_descs[1].location = 1;
    attrib_descs[1].format   = vk::Format::eR32G32B32A32Sfloat;
    attrib_descs[1].offset   = offsetof(LineVertex, color);
    attrib_descs[2].binding  = 0;
    attrib_descs[2].location = 2;
    attrib_descs[2].format   = vk::Format::eR32Sfloat;
    attrib_descs[2].offset   = offsetof(LineVertex, param);

    // --- Shader stages ---
    std::vector<vk::PipelineShaderStageCreateInfo> stages;
    for (auto const& stage : pipeline_data.shader_stages)
    {
        vk::PipelineShaderStageCreateInfo si{};
        si.sType  = vk::StructureType::ePipelineShaderStageCreateInfo;
        si.stage  = stage.stage;
        si.module = stage.module;
        si.pName  = "main";
        stages.push_back(si);
    }

    // --- Dynamic states: viewport, scissor, line width ---
    std::vector<vk::DynamicState> dynamic_states{
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
        vk::DynamicState::eLineWidth,
    };
    vk::PipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = vk::StructureType::ePipelineDynamicStateCreateInfo;
    dynamic_state.setDynamicStates(dynamic_states);

    // --- Vertex input state ---
    vk::PipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = vk::StructureType::ePipelineVertexInputStateCreateInfo;
    vertex_input_info.setVertexBindingDescriptions(binding_desc);
    vertex_input_info.setVertexAttributeDescriptions(attrib_descs);

    // --- Input assembly: LINE_LIST ---
    vk::PipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = vk::StructureType::ePipelineInputAssemblyStateCreateInfo;
    input_assembly.setTopology(vk::PrimitiveTopology::eLineList);
    input_assembly.setPrimitiveRestartEnable(false);

    // --- Viewport (dynamic, placeholders required) ---
    vk::Viewport viewport{};
    viewport.x = 0; viewport.y = 0;
    viewport.width    = (float)state.swap_chain.extent.width;
    viewport.height   = (float)state.swap_chain.extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vk::Rect2D scissor{};
    scissor.offset = vk::Offset2D(0, 0);
    scissor.extent = state.swap_chain.extent;
    vk::PipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = vk::StructureType::ePipelineViewportStateCreateInfo;
    viewport_state.setViewportCount(1); viewport_state.setViewports(viewport);
    viewport_state.setScissorCount(1);  viewport_state.setScissors(scissor);

    // --- Rasterizer: no culling, line width dynamic ---
    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType               = vk::StructureType::ePipelineRasterizationStateCreateInfo;
    rasterizer.depthClampEnable    = false;
    rasterizer.rasterizerDiscardEnable = false;
    rasterizer.polygonMode         = vk::PolygonMode::eFill;
    rasterizer.lineWidth           = 1.0f;   // overridden each draw via dynamic state
    rasterizer.cullMode            = vk::CullModeFlagBits::eNone;
    rasterizer.frontFace           = vk::FrontFace::eCounterClockwise;
    rasterizer.depthBiasEnable     = false;

    // --- Multisampling ---
    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = vk::StructureType::ePipelineMultisampleStateCreateInfo;
    multisampling.rasterizationSamples = state.msaa;
    multisampling.sampleShadingEnable  = false;
    multisampling.minSampleShading     = 1.0f;

    // --- Alpha blending enabled ---
    vk::PipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    blend_attachment.blendEnable         = true;
    blend_attachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    blend_attachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    blend_attachment.colorBlendOp        = vk::BlendOp::eAdd;
    blend_attachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    blend_attachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    blend_attachment.alphaBlendOp        = vk::BlendOp::eAdd;

    vk::PipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType           = vk::StructureType::ePipelineColorBlendStateCreateInfo;
    color_blending.logicOpEnable   = false;
    color_blending.attachmentCount = 1;
    color_blending.setAttachments(blend_attachment);

    // --- Depth: test (lessOrEqual) but no write — lines render over/under geometry ---
    vk::PipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType                = vk::StructureType::ePipelineDepthStencilStateCreateInfo;
    depth_stencil.depthTestEnable      = true;
    depth_stencil.depthWriteEnable     = false;
    depth_stencil.depthCompareOp       = vk::CompareOp::eLessOrEqual;
    depth_stencil.depthBoundsTestEnable = false;
    depth_stencil.stencilTestEnable    = false;

    // --- Pipeline layout with push constant for dash_count (fragment) ---
    vk::PushConstantRange push_range{};
    push_range.setStageFlags(vk::ShaderStageFlagBits::eFragment);
    push_range.setOffset(0);
    push_range.setSize(2 * sizeof(float));  // {dash_count, alpha}

    // Use the non-RAII device handle so createPipelineLayout / createGraphicsPipelines
    // return plain vk::PipelineLayout / vk::Pipeline (non-owning) instead of RAII
    // wrappers that would destroy the handles when they fall off the stack.
    vk::Device device = state.device;

    vk::PipelineLayoutCreateInfo layout_info{};
    layout_info.sType = vk::StructureType::ePipelineLayoutCreateInfo;
    layout_info.setSetLayouts(pipeline_data.descriptor_set_layouts);
    layout_info.setPushConstantRanges(push_range);

    auto pipeline_layout = device.createPipelineLayout(layout_info).value;

    // --- Graphics pipeline ---
    vk::GraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType      = vk::StructureType::eGraphicsPipelineCreateInfo;
    pipeline_info.stageCount = (uint32_t)stages.size();
    pipeline_info.setStages(stages);
    pipeline_info.setPVertexInputState(&vertex_input_info);
    pipeline_info.setPInputAssemblyState(&input_assembly);
    pipeline_info.setPViewportState(&viewport_state);
    pipeline_info.setPRasterizationState(&rasterizer);
    pipeline_info.setPMultisampleState(&multisampling);
    pipeline_info.setPDepthStencilState(&depth_stencil);
    pipeline_info.setPColorBlendState(&color_blending);
    pipeline_info.setPDynamicState(&dynamic_state);
    pipeline_info.setLayout(pipeline_layout);
    pipeline_info.setRenderPass(render_pass);
    pipeline_info.setSubpass(0);
    pipeline_info.basePipelineIndex = -1;

    auto pipelines = device.createGraphicsPipelines(VK_NULL_HANDLE, pipeline_info);
    checkResult(pipelines.result);
    vk::Pipeline vk_pipeline = pipelines.value[0];

    auto pipeline_finish = bindPipeline(pipeline_data, vk_pipeline, pipeline_layout);

    // Bind world buffer to Set 0
    updateUniformBuffer<WorldBufferObject>(state.device,
                                          world_buffer,
                                          pipeline_finish.descriptor_sets[0].set,
                                          pipeline_finish.descriptor_sets[0].layout_bindings[0],
                                          1);

    // Bind model buffer to Set 1
    updateUniformBuffer<ModelBufferObject>(state.device,
                                          model_buffer,
                                          pipeline_finish.descriptor_sets[1].set,
                                          pipeline_finish.descriptor_sets[1].layout_bindings[0],
                                          20);

    return pipeline_finish;
}
