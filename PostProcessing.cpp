#include "PostProcessing.h"
#include "Material.h"
#include "Program.h"
#include "Scene.h"
#include "Renderer.h"
#include "VulkanRenderSystem.h"
#include <vulkan/vulkan_enums.hpp>
#include <imgui.h>
#include "imgui_impl_vulkan.h"

static std::unique_ptr<Program> createPostProcessingProgram(RenderingState const& state, vk::RenderPass const& render_pass)
{
    layer_types::Program program_desc;
    program_desc.vertex_shader= {{"./shaders/post_processing_vert.spv"}};
    program_desc.fragment_shader = {{"./shaders/post_processing_frag.spv"}};
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"colot_attachment"}},
        .type = layer_types::BufferType::NoBuffer,
        .size = 1,
        .binding = layer_types::Binding {
            .binding = 0,
            .type = layer_types::BindingType::TextureSampler,
            .size = 1,
            .vertex = true,
            .fragment = true
        }
    }});
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"fog_buffer"}},
        .type = layer_types::BufferType::NoBuffer,
        .size = 1,
        .binding = layer_types::Binding {
            .binding = 0,
            .type = layer_types::BindingType::TextureSampler,
            .size = 1,
            .vertex = true,
            .fragment = true,
        }
    }});
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"depth_attachment"}},
        .type = layer_types::BufferType::NoBuffer,
        .size = 1,
        .binding = layer_types::Binding {
            .binding = 0,
            .type = layer_types::BindingType::TextureSampler,
            .size = 1,
            .vertex = true,
            .fragment = true,
        }
    }});
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"world_buffer"}},
        .type = layer_types::BufferType::WorldBufferObject,
        .size = 1,
        .binding = layer_types::Binding {
            .name = {{"binding world"}},
            .binding = 0,
            .type = layer_types::BindingType::Uniform,
            .size = 1,
            .fragment = true,
        }
    }});
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"fog param buffer"}},
        .type = layer_types::BufferType::FogVolumeObject,
        .size = 1,
        .binding = layer_types::Binding {
            .name = {{"binding fog param"}},
            .binding = 0,
            .type = layer_types::BindingType::Uniform,
            .size = 1,
            .fragment = true,
        }
    }});
    
    // Make the program like this for now. Update the descriptor set each frame
    // with the correct color attachment.
    return createProgram(program_desc, state, {}, render_pass, "Post Processing");
}

static std::unique_ptr<Program> createComputeFogProgram(RenderingState const& state, vk::RenderPass const& render_pass)
{
    layer_types::Program program_desc;
    program_desc.compute_shader = {("./shaders/fog.spv")};
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"binding image"}},
        .type = layer_types::BufferType::NoBuffer,
        .size = 1,
        .binding = layer_types::Binding {
            .name = {{"binding image"}},
            .binding = 0,
            .type = layer_types::BindingType::StorageImage,
            .size = 1,
            .compute = true,
        }
    }});
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"texture_buffer"}},
        .type = layer_types::BufferType::NoBuffer,
        .size = 1,
        .binding = layer_types::Binding {
            .name = {{"binding textures"}},
            .binding = 0,
            .type = layer_types::BindingType::TextureSampler,
            .size = 1,
            .compute = true
        }
    }});
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"test_program"}},
        .type = layer_types::BufferType::WorldBufferObject,
        .size = 1,
        .binding = layer_types::Binding {
            .name = {{"binding world"}},
            .binding = 0,
            .type = layer_types::BindingType::Uniform,
            .size = 1,
            .compute = true
        }
    }});
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"Fog Volume"}},
        .type = layer_types::BufferType::FogVolumeObject,
        .size = 1,
        .binding = layer_types::Binding {
            .name = {{"Fog Volume"}},
            .binding = 0,
            .type = layer_types::BindingType::Uniform,
            .size = 1,
            .compute = true
        }
    }});

    return createProgram(program_desc, state, {}, render_pass, "Compute Fog");
}

inline void postProcessingUpdateDescriptorSets(RenderingState const& state,
                                               PostProcessing const& post_processing,
                                               size_t image_index)
{
    auto& ppp = post_processing;

    auto color_res_set = ppp.program->program.descriptor_sets[0].set[state.current_frame];
    auto color_res_binding = ppp.program->program.descriptor_sets[0].layout_bindings[0];

    auto& color_attachment = state.framebuffer_resources[image_index].color_resources;
    updateImageSampler(state.device, {color_attachment.depth_image_view},
                       post_processing.sampler, {color_res_set}, color_res_binding);

    auto desc_set_blender = ppp.program->program.descriptor_sets[1].set[state.current_frame];
    auto desc_binding_blender = ppp.program->program.descriptor_sets[1].layout_bindings[0];
    updateImageSampler(state.device, {ppp.fog_buffer[state.current_frame].second}, ppp.sampler, {desc_set_blender}, desc_binding_blender);

    auto ppp_depth_descriptor_set = ppp.program->program.descriptor_sets[2].set[state.current_frame];
    auto ppp_depth_descriptor_binding= ppp.program->program.descriptor_sets[2].layout_bindings[0];
    updateImageSampler(state.device, {state.framebuffer_resources[image_index].depth_resolved_resources.depth_image_view}, ppp.sampler,
                       {ppp_depth_descriptor_set}, ppp_depth_descriptor_binding);

    // Bind the depth buffer to the fog program texture sampler
    auto depth_descriptor_set = ppp.fog_compute_program->program.descriptor_sets[1].set[state.current_frame];
    auto depth_descriptor_binding = ppp.fog_compute_program->program.descriptor_sets[1].layout_bindings[0];

    DepthResources const& resources = state.framebuffer_resources[image_index].depth_resolved_resources;
    Texture depth_texture{.image=resources.depth_image, .memory=resources.depth_image_memory,
                         .view=resources.depth_image_view,
                         .sampler=ppp.sampler,
                         .name="",
                         .mip_levels=1};
    updateImageSampler(state.device, {depth_texture}, {depth_descriptor_set}, depth_descriptor_binding);

    // Update the 3D texture descriptor set
    auto desc_set = ppp.fog_compute_program->program.descriptor_sets[0].set[state.current_frame];
    auto desc_binding = ppp.fog_compute_program->program.descriptor_sets[0].layout_bindings[0];
    updateImage(state.device, ppp.fog_buffer[state.current_frame].second, {desc_set}, desc_binding);

}

static void postProcessingDraw(vk::CommandBuffer& command_buffer,
                        Scene const& scene,
                        PostProcessing const& ppp,
                        size_t frame)
{
    auto& program = ppp.program;

    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                ppp.program->program.pipeline[0]);

    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            program->program.pipeline_layout,
                            0,
                            1,
                            &program->program.descriptor_sets[0].set[frame],
                            0,
                            nullptr);


    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            program->program.pipeline_layout,
                            1,
                            1,
                            &program->program.descriptor_sets[1].set[frame],
                            0,
                            nullptr);

    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            program->program.pipeline_layout,
                            2,
                            1,
                            &program->program.descriptor_sets[2].set[frame],
                            0,
                            nullptr);

    uint32_t offset{};
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            program->program.pipeline_layout,
                            3,
                            1,
                            &program->program.descriptor_sets[3].set[frame],
                            1,
                            &offset);

    offset = 0;
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            program->program.pipeline_layout,
                            4,
                            1,
                            &program->program.descriptor_sets[4].set[frame],
                            1,
                            &offset);

    for (auto& buffer : program->buffers)
    {
        if (auto* world = std::get_if<buffer_types::World>(&buffer))
        {
            auto ubo = createWorldBufferObject(scene);
            writeBuffer(world->buffers[frame], ubo);
        }
        else if (auto* fog = std::get_if<buffer_types::FogVolume>(&buffer))
        {
            writeBuffer(fog->buffers[frame], scene.fog);
        }
    }

    command_buffer.draw(6,1,0,0);
}

void postProcessingRenderPass(RenderingState const& state,
                              PostProcessing& ppp,
                              vk::CommandBuffer& command_buffer,
                              Scene const& scene,
                              size_t image_index)
{
    vk::RenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = vk::StructureType::eRenderPassBeginInfo;
    render_pass_info.setRenderPass(ppp.render_pass);
    render_pass_info.setFramebuffer(ppp.framebuffer[image_index]);
    render_pass_info.renderArea.offset = vk::Offset2D(0,0);
    render_pass_info.renderArea.extent = state.swap_chain.extent;

    std::array<vk::ClearValue, 2> clear_values{};
    clear_values[0].color = vk::ClearColorValue(std::array<float,4>{0.3984,0.695,1});
    clear_values[1].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);

    render_pass_info.clearValueCount = clear_values.size();
    render_pass_info.setClearValues(clear_values);

    vk::Viewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(state.swap_chain.extent.width);
    viewport.height = static_cast<float>(state.swap_chain.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    command_buffer.setViewport(0,viewport);
    
    vk::Rect2D scissor{};
    scissor.offset = vk::Offset2D{0,0};
    scissor.extent = state.swap_chain.extent;
    command_buffer.setScissor(0,scissor);

    transitionImageLayout(command_buffer, state.framebuffer_resources[image_index].color_resources.depth_image,
                                 state.swap_chain.swap_chain_image_format,
                                 vk::ImageLayout::eColorAttachmentOptimal,
                                 vk::ImageLayout::eShaderReadOnlyOptimal,
                                 1);

    transitionImageLayout(command_buffer, state.framebuffer_resources[image_index].depth_resolved_resources.depth_image, vk::Format::eD32Sfloat,
        vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 1);

    postProcessingUpdateDescriptorSets(state, ppp, image_index);

    if (scene.fog.volumetric_fog_enabled)
    {
        runPipeline(command_buffer, scene, ppp.fog_compute_program, state.current_frame);
    }
    transitionImageLayout(command_buffer, ppp.fog_buffer[state.current_frame].first, vk::Format::eR16Sfloat, vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal, 1);

    command_buffer.beginRenderPass(render_pass_info,
                                   vk::SubpassContents::eInline);

    postProcessingDraw(command_buffer, scene, ppp, state.current_frame);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
    command_buffer.endRenderPass();

    transitionImageLayout(command_buffer, ppp.fog_buffer[state.current_frame].first, vk::Format::eR16Sfloat, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eGeneral, 1);
}

static std::vector<vk::Framebuffer> createPostProcessingFramebuffers(RenderingState const& state,
                                                                     vk::RenderPass const& render_pass)
{
    std::vector<vk::Framebuffer> framebuffers;
    for (auto const& swap_chain_image_view : state.image_views)
    {
        auto color_resources = createColorResources(state);
        std::array<vk::ImageView, 2> attachments {{color_resources.depth_image_view, swap_chain_image_view}};

        vk::FramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = vk::StructureType::eFramebufferCreateInfo;
        framebuffer_info.setRenderPass(render_pass);
        framebuffer_info.setAttachments(attachments);
        framebuffer_info.width = state.swap_chain.extent.width;
        framebuffer_info.height = state.swap_chain.extent.height;
        framebuffer_info.layers = 1;
        
        auto result = state.device.createFramebuffer(framebuffer_info);
        checkResult(result.result);
        framebuffers.push_back(result.value);
    }

    return framebuffers;
}

static vk::RenderPass createPostProcessingRenderPass(vk::Format const swap_chain_image_format,
                                                     vk::Device const& device,
                                                     vk::SampleCountFlagBits msaa)
{
    vk::AttachmentDescription color_attachment{};
    color_attachment.format = swap_chain_image_format;
    color_attachment.samples = msaa;
    color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
    color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
    color_attachment.stencilLoadOp =  vk::AttachmentLoadOp::eDontCare;
    color_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    color_attachment.initialLayout = vk::ImageLayout::eUndefined;
    color_attachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::AttachmentDescription color_attachment_resolve{};
    color_attachment_resolve.format = swap_chain_image_format;
    color_attachment_resolve.samples = vk::SampleCountFlagBits::e1;
    color_attachment_resolve.loadOp = vk::AttachmentLoadOp::eDontCare;
    color_attachment_resolve.storeOp = vk::AttachmentStoreOp::eStore;
    color_attachment_resolve.stencilLoadOp =  vk::AttachmentLoadOp::eDontCare;
    color_attachment_resolve.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    color_attachment_resolve.initialLayout = vk::ImageLayout::eUndefined;
    color_attachment_resolve.finalLayout = vk::ImageLayout::ePresentSrcKHR;

    vk::AttachmentReference color_attachment_ref;
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::AttachmentReference color_attachment_resolve_ref;
    color_attachment_resolve_ref.attachment = 1;
    color_attachment_resolve_ref.layout = vk::ImageLayout::eColorAttachmentOptimal;

    std::vector<vk::AttachmentReference> resolve_refs{color_attachment_resolve_ref};

    vk::SubpassDescription subpass {};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 2;
    subpass.setColorAttachments(color_attachment_ref);
    subpass.setResolveAttachments(resolve_refs);

    vk::SubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;  // Previous render pass
    dependency.dstSubpass = 0;  // This post-processing subpass
    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
    dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead;

    std::array<vk::AttachmentDescription, 2> attachments {color_attachment, color_attachment_resolve};
    vk::RenderPassCreateInfo renderPassInfo{};
    renderPassInfo.setAttachments(attachments);
    renderPassInfo.setSubpasses(subpass);
    renderPassInfo.setDependencies(dependency);

    // Create the render pass
    auto render_pass = device.createRenderPass(renderPassInfo);

    checkResult(render_pass.result);
    return render_pass.value;
}

PostProcessing createPostProcessing(RenderingState const& state)
{
    PostProcessing pp;
    pp.sampler = createTextureSampler(state);

    pp.fog_buffer = createFogBuffer(state, vk::MemoryPropertyFlagBits::eDeviceLocal);

    std::cout << "Creating post processing render pass\n";
    pp.render_pass = createPostProcessingRenderPass(state.swap_chain.swap_chain_image_format, state.device, state.msaa);

    std::cout << "Creating post processing framebuffer\n";
    pp.framebuffer = createPostProcessingFramebuffers(state, pp.render_pass);

    std::cout << "Creating post processing program\n";
    pp.program = createPostProcessingProgram(state, pp.render_pass);

    std::cout << "Creating compute fog program\n";
    pp.fog_compute_program= createComputeFogProgram(state, pp.render_pass);

    return pp;
}