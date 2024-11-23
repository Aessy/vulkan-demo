#pragma once

#include "VulkanRenderSystem.h"

#include "Program.h"
#include "Scene.h"

struct ShadowMapFramebuffer
{
    // cascades_n framebuffers per frame
    std::array<std::vector<std::unique_ptr<vk::raii::Framebuffer>>, 2> framebuffers;

    // one cascade image per frame
    std::vector<std::unique_ptr<vk::raii::Image>> cascade_images;

    // one image view array per frame.
    std::vector<std::unique_ptr<vk::raii::ImageView>> image_views;
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
    std::vector<std::unique_ptr<UniformBuffer>> cascaded_shadow_map_buffer;
};

CascadedShadowMap createCascadedShadowMap(RenderingState const& core, Scene const& scene);

void shadowMapRenderPass(RenderingState const& state, CascadedShadowMap& shadow_map, Scene const& scene, vk::CommandBuffer const& command_buffer);