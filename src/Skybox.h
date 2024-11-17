#pragma once

#include "VulkanRenderSystem.h"
#include "Program.h"
#include <vulkan/vulkan_handles.hpp>

struct Skybox
{
    vk::Image image;
    vk::ImageView view;
    std::unique_ptr<Program> program;

    vk::Sampler sampler;
};

Skybox createSkybox(RenderingState const& state, vk::RenderPass render_pass, std::array<std::string, 6> skybox_paths);