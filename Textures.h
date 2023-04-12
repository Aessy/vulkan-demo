#pragma once

#define VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_ASSERT_ON_RESULT
#include "vulkan/vulkan.hpp"

#include "VulkanRenderSystem.h"

#include <vector>

struct Texture
{
    vk::Image image;
    vk::DeviceMemory memory;
    vk::ImageView view;
    vk::Sampler sampler;

    std::string const name;
};

struct Textures
{
    vk::Sampler sampler;

    std::vector<Texture> textures;
};

Textures createTextures(RenderingState const& core, std::vector<std::string> const& paths);