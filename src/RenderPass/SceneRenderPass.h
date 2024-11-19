#pragma once

#include "RenderPass/ShadowMap.h"
#include "VulkanRenderSystem.h"

#include "ShadowMap.h"
#include "Program.h"
#include "Scene.h"

struct SceneFramebufferState
{
    vk::Framebuffer framebuffer;

    DepthResources color_resource;
    DepthResources depth_resource;
    DepthResources depth_resolve_resource;
};

struct SceneRenderPass
{
    vk::RenderPass render_pass;
    std::vector<SceneFramebufferState> framebuffers;

    // A pipeline is bound to a render pass. So it makes sense that all pipelines that can be run is here.
    std::vector<Pipeline> pipelines;
};

void sceneRenderPass(vk::CommandBuffer command_buffer,
                     RenderingState const& state,
                     SceneRenderPass& scene_render_pass,
                     Scene const& scene_data,
                     uint32_t image_index);

SceneRenderPass createSceneRenderPass(RenderingState const& state, Textures const& textures, CascadedShadowMap const& shadow_map);