#pragma once

#include <vulkan/vulkan_core.h>

#include "VulkanRenderSystem.h"
#include "Program.h"
#include "Scene.h"
#include "RenderPass/SceneRenderPass.h"

struct PostProcessing
{
    vk::RenderPass render_pass;
    std::vector<vk::Framebuffer> framebuffer;

    DepthResources color_attachment;
    std::unique_ptr<Program> program;

    vk::Sampler sampler;
    
    std::vector<std::pair<vk::Image, vk::ImageView>> fog_buffer;
    std::unique_ptr<Program> fog_compute_program;

    PostProcessingBufferObject buffer_object{};

    std::vector<DepthResources> in_depth_attachment;
    std::vector<DepthResources> in_color_attachment;
};

PostProcessing createPostProcessing(RenderingState const& state,
                                    SceneRenderPass const& scene);

void postProcessingRenderPass(RenderingState const& state,
                              PostProcessing& ppp,
                              vk::CommandBuffer& command_buffer,
                              Scene const& scene,
                              size_t image_index);