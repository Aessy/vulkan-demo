#pragma once

#include <vulkan/vulkan_core.h>

#include "Model.h"
#include "VulkanRenderSystem.h"
#include "Program.h"
#include "Scene.h"
#include "RenderPass/SceneRenderPass.h"

struct PostProcessing
{
    vk::RenderPass render_pass;
    std::vector<vk::Framebuffer> framebuffer;

    std::unique_ptr<Program> program;

    vk::Sampler sampler;
    
    std::vector<std::unique_ptr<ImageResource>> fog_buffer;
    std::unique_ptr<Program> fog_compute_program;

    PostProcessingBufferObject buffer_object{};
    FogVolumeBufferObject fog_object{};

    std::vector<std::unique_ptr<UniformBuffer>> fog_data_buffer;
    std::vector<std::unique_ptr<UniformBuffer>> post_processing_buffer;
};

PostProcessing createPostProcessing(RenderingState const& state,
                                    SceneRenderPass const& scene_render_pass,
                                    std::vector<std::unique_ptr<UniformBuffer>> const& world_buffer);

void postProcessingRenderPass(RenderingState const& state,
                              PostProcessing& ppp,
                              vk::CommandBuffer const& command_buffer,
                              Scene const& scene,
                              size_t image_index);