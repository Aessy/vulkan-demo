#pragma once

#include <vulkan/vulkan_core.h>

#include "VulkanRenderSystem.h"
#include "Program.h"
#include "Scene.h"

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
};

PostProcessing createPostProcessing(RenderingState const& state);

void postProcessingRenderPass(RenderingState const& state,
                              PostProcessing& ppp,
                              vk::CommandBuffer& command_buffer,
                              Scene const& scene,
                              size_t image_index);