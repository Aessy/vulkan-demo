#pragma once

#include "VulkanRenderSystem.h"

#include "Program.h"
#include "Scene.h"

struct CascadedShadowMap
{
    vk::RenderPass render_pass;
    std::array<std::vector<std::pair<vk::Framebuffer,vk::ImageView>>, 2> framebuffer;

    Pipeline pipeline;

    static constexpr uint32_t n_cascaded_shadow_maps = 5;

    // Just recalculate per render call probably. Don't need to store it
    std::array<glm::mat4, n_cascaded_shadow_maps> light_projection_views;
};

CascadedShadowMap createCascadedShadowMap(RenderingState const& core);

void shadowMapRenderPass(RenderingState const& state, CascadedShadowMap& shadow_map, Scene const& scene, vk::CommandBuffer command_buffer);