#pragma once

#include "VulkanRenderSystem.h"

#include "Program.h"
#include "Scene.h"

struct ShadowMapFramebuffer
{
    // cascades_n framebuffers per frame
    std::array<std::vector<vk::Framebuffer>, 2> framebuffers;

    // one cascade image per frame
    std::array<vk::Image, 2> cascade_images;

    // one image view array per frame.
    std::array<vk::ImageView, 2> image_views;
};

struct CascadedShadowMap
{
    vk::RenderPass render_pass;
    
    ShadowMapFramebuffer framebuffer_data;

    Pipeline pipeline;

    static constexpr uint32_t n_cascaded_shadow_maps = 5;

    // Just recalculate per render call probably. Don't need to store it
    std::array<glm::mat4, n_cascaded_shadow_maps> light_projection_views;

    // Buffer objects for cascaded shadow map.
    std::vector<UniformBuffer> cascaded_shadow_map_buffer;
};

CascadedShadowMap createCascadedShadowMap(RenderingState const& core);

void shadowMapRenderPass(RenderingState const& state, CascadedShadowMap& shadow_map, Scene const& scene, vk::CommandBuffer command_buffer);