#include "PostProcessing.h"

#include "Model.h"
#include "VulkanRenderSystem.h"
#include "descriptor_set.h"
#include "Program.h"
#include "Scene.h"
#include "Renderer.h"

#include "Pipelines/Pipeline.h"

#include "imgui_impl_vulkan.h"

#include <vulkan/vulkan_enums.hpp>
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <vulkan/vulkan_raii.hpp>

static Pipeline createPostProcessingProgram(RenderingState const& state, vk::RenderPass const& render_pass)
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
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"Post Processing Data"}},
        .type = layer_types::BufferType::PostProcessingDataBufferObject,
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

    auto pipeline_data = createPipelineData(state, program_desc);
    auto [pipeline, pipeline_layout] = createPipeline(pipeline_data, state.swap_chain.extent, state.device, render_pass, state.msaa);

    auto pipeline_finish = bindPipeline(pipeline_data, pipeline, pipeline_layout);

    return pipeline_finish;
}

static Pipeline createComputeFogProgram(RenderingState const& state, vk::RenderPass const& render_pass)
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

    auto pipeline_data = createPipelineData(state, program_desc);
    auto [pipeline, pipeline_layout] = createComputePipeline2(pipeline_data, state.device);

    return bindPipeline(pipeline_data, pipeline, pipeline_layout);
}

inline void postProcessingUpdateDescriptorSets(RenderingState const& state,
                                               PostProcessing const& post_processing,
                                               size_t image_index)
{}

static void postProcessingDraw(vk::CommandBuffer const& command_buffer,
                        Scene const& scene,
                        PostProcessing const& ppp,
                        size_t frame)
{
    auto& program = ppp.program;

    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                ppp.program.pipeline);

    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            program.pipeline_layout,
                            0,
                            1,
                            &program.descriptor_sets[0].set[frame],
                            0,
                            nullptr);


    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            program.pipeline_layout,
                            1,
                            1,
                            &program.descriptor_sets[1].set[frame],
                            0,
                            nullptr);

    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            program.pipeline_layout,
                            2,
                            1,
                            &program.descriptor_sets[2].set[frame],
                            0,
                            nullptr);

    uint32_t offset{};
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            program.pipeline_layout,
                            3,
                            1,
                            &program.descriptor_sets[3].set[frame],
                            1,
                            &offset);

    offset = 0;
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            program.pipeline_layout,
                            4,
                            1,
                            &program.descriptor_sets[4].set[frame],
                            1,
                            &offset);
    offset = 0;
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            program.pipeline_layout,
                            5,
                            1,
                            &program.descriptor_sets[5].set[frame],
                            1,
                            &offset);

    command_buffer.draw(6,1,0,0);
}

void postProcessingRenderPass(RenderingState const& state,
                              PostProcessing& ppp,
                              vk::raii::CommandBuffer const& command_buffer,
                              Scene const& scene,
                              size_t image_index)
{
    vk::Framebuffer framebuffer = ppp.framebuffer[image_index]->framebuffer;
    vk::RenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = vk::StructureType::eRenderPassBeginInfo;
    render_pass_info.setRenderPass(ppp.render_pass);
    render_pass_info.setFramebuffer(framebuffer);
    render_pass_info.renderArea.offset = vk::Offset2D(0,0);
    render_pass_info.renderArea.extent = state.swap_chain.extent;

    std::array<vk::ClearValue, 2> clear_values{};
    clear_values[0].color = vk::ClearColorValue(std::array<float,4>{0.3984,0.695,1});
    clear_values[1].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);

    render_pass_info.clearValueCount = clear_values.size();
    render_pass_info.setClearValues(clear_values);

    vk::Viewport viewport{};
    viewport.x = 0.0f;
    viewport.y = static_cast<float>(state.swap_chain.extent.height);
    viewport.width = static_cast<float>(state.swap_chain.extent.width);
    viewport.height = -static_cast<float>(state.swap_chain.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    command_buffer.setViewport(0,viewport);
    
    vk::Rect2D scissor{};
    scissor.offset = vk::Offset2D{0,0};
    scissor.extent = state.swap_chain.extent;
    command_buffer.setScissor(0,scissor);

    postProcessingUpdateDescriptorSets(state, ppp, image_index);

    if (scene.fog.volumetric_fog_enabled)
    {
        runPipeline(command_buffer, scene, ppp.fog_compute_program, state.current_frame);
    }
    transitionImageLayout(command_buffer, ppp.fog_buffer[state.current_frame]->image, vk::Format::eR16Sfloat, vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal, 1);

    command_buffer.beginRenderPass(render_pass_info,
                                   vk::SubpassContents::eInline);

    postProcessingDraw(command_buffer, scene, ppp, state.current_frame);

    vk::CommandBuffer v = command_buffer;

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), v);
    command_buffer.endRenderPass();

    transitionImageLayout(command_buffer, ppp.fog_buffer[state.current_frame]->image, vk::Format::eR16Sfloat, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eGeneral, 1);
}

static std::vector<std::unique_ptr<PostProcessingFramebuffer>> createPostProcessingFramebuffers(RenderingState const& state,
                                                                     vk::RenderPass const& render_pass)
{
    std::vector<std::unique_ptr<PostProcessingFramebuffer>> framebuffers;
    for (auto const& swap_chain_image_view : state.image_views)
    {
        auto color_resources = createColorResources(state, state.swap_chain.swap_chain_image_format);
        std::array<vk::ImageView, 2> attachments {{color_resources.depth_image_view, swap_chain_image_view}};

        vk::FramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = vk::StructureType::eFramebufferCreateInfo;
        framebuffer_info.setRenderPass(render_pass);
        framebuffer_info.setAttachments(attachments);
        framebuffer_info.width = state.swap_chain.extent.width;
        framebuffer_info.height = state.swap_chain.extent.height;
        framebuffer_info.layers = 1;
        
        auto result = state.device.createFramebuffer(framebuffer_info);

        PostProcessingFramebuffer framebuffer {
            .framebuffer = std::move(result.value()),
            .resources = std::move(color_resources)
        };

        framebuffers.push_back(std::make_unique<PostProcessingFramebuffer>(std::move(framebuffer)));
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

/*
Texture createNoiseTexture(RenderingState const& state, Pipeline const& program, vk::Sampler sampler)
{
    auto texture = createTexture(state,
                                 "./textures/blue_noise.png",
                                 TextureType::Map,
                                 vk::Format::eR8G8B8A8Unorm,
                                 sampler);

    for (int i = 0; i < 2; ++i)
    {
        updateImageSampler(state.device, texture, program.descriptor_sets[1].set[i],
                                    program.descriptor_sets[1].layout_bindings[0]);
    }
    return texture;
}
*/

PostProcessing createPostProcessing(RenderingState const& state,
                                    SceneRenderPass const& scene_render_pass,
                                    std::vector<std::unique_ptr<UniformBuffer>> const& world_buffer)
{
    auto sampler = createTextureSampler(state, false);

    std::vector<std::unique_ptr<ImageResource>> fog_buffer = createFogBuffer(state, vk::MemoryPropertyFlagBits::eDeviceLocal);

    spdlog::info("Creating post processing render pass");
    auto render_pass = createPostProcessingRenderPass(state.swap_chain.swap_chain_image_format, state.device, state.msaa);

    spdlog::info("Creating post processing framebuffer");
    auto framebuffer = createPostProcessingFramebuffers(state, render_pass);

    spdlog::info("Creating post processing program");
    auto program = createPostProcessingProgram(state, render_pass);

    spdlog::info("Creating compute fog program");
    auto fog_compute_program = createComputeFogProgram(state, render_pass);

    auto fog_data_buffer = createUniformBuffers<FogVolumeBufferObject>(state, 1);
    auto post_processing_buffer = createUniformBuffers<PostProcessingBufferObject>(state, 1);

    for (int i = 0; i < 2; ++i)
    {
        vk::ImageView color_resource = scene_render_pass.framebuffers[i]->color_resource.depth_image_view;
        auto color_res_set = program.descriptor_sets[0].set[i];
        auto color_res_binding = program.descriptor_sets[0].layout_bindings[0];
        updateImageSampler(state.device,
                        {color_resource},
                        sampler,
                        {color_res_set},
                        color_res_binding);

        vk::ImageView fog_image_view = fog_buffer[i]->image_view;
        auto fog_set= program.descriptor_sets[1].set[i];
        auto fog_binding = program.descriptor_sets[1].layout_bindings[0];
        updateImageSampler(state.device,
                        {fog_image_view}, 
                        sampler,
                        {fog_set},
                        fog_binding);

        vk::ImageView depth_resource = scene_render_pass.framebuffers[i]->depth_resolve_resource.depth_image_view;
        auto depth_res_set = program.descriptor_sets[2].set[i];
        auto depth_res_binding = program.descriptor_sets[2].layout_bindings[0];
        updateImageSampler(state.device,
                        {depth_resource},
                        sampler,
                        {depth_res_set},
                        depth_res_binding);
    }

    updateUniformBuffer<WorldBufferObject>(state.device,
                                           world_buffer,
                                           program.descriptor_sets[3].set,
                                           program.descriptor_sets[3].layout_bindings[0],
                                           1);

    updateUniformBuffer<FogVolumeBufferObject>(state.device,
                                           fog_data_buffer,
                                           program.descriptor_sets[4].set,
                                           program.descriptor_sets[4].layout_bindings[0],
                                           1);

    updateUniformBuffer<PostProcessingBufferObject>(state.device,
                                                    post_processing_buffer,
                                                    program.descriptor_sets[5].set,
                                                    program.descriptor_sets[5].layout_bindings[0],
                                                    1);

    for (int i = 0; i < 2; ++i)
    {
        vk::ImageView fog_image_view = fog_buffer[i]->image_view;
        // Update the 3D texture descriptor set
        auto desc_set = fog_compute_program.descriptor_sets[0].set[i];
        auto desc_binding = fog_compute_program.descriptor_sets[0].layout_bindings[0];
        updateImage(state.device, fog_image_view, {desc_set}, desc_binding);
    }

    updateUniformBuffer<WorldBufferObject>(state.device,
                                           world_buffer,
                                           fog_compute_program.descriptor_sets[1].set,
                                           fog_compute_program.descriptor_sets[1].layout_bindings[0],
                                           1);

    updateUniformBuffer<FogVolumeBufferObject>(state.device,
                                           fog_data_buffer,
                                           fog_compute_program.descriptor_sets[2].set,
                                           fog_compute_program.descriptor_sets[2].layout_bindings[0],
                                           1);
    

    PostProcessing pp {
        .render_pass = std::move(render_pass),
        .framebuffer = std::move(framebuffer),
        .program = std::move(program),
        .sampler = std::move(sampler),
        .fog_buffer = std::move(fog_buffer),
        .fog_compute_program = std::move(program),
        .fog_data_buffer = std::move(fog_data_buffer),
        .post_processing_buffer = std::move(post_processing_buffer)
    };

    return pp;
}

void postProcessingWriteBuffers(PostProcessing& post_processing, int frame)
{
    writeBuffer(*post_processing.post_processing_buffer[frame], post_processing.buffer_object);
    writeBuffer(*post_processing.fog_data_buffer[frame], post_processing.fog_object);
}