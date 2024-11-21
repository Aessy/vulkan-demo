#pragma once

#include "VulkanRenderSystem.h"

#include <vector>

struct Texture
{
    vk::Image image;
    vk::DeviceMemory memory;
    vk::ImageView view;
    vk::Sampler sampler;

    std::string const name;

    uint32_t mip_levels;
};

struct Textures
{
    std::vector<Texture> textures;
};

enum class TextureType
{
    MipMap,
    Map
};

struct TextureInput
{
    std::string path;
    TextureType texture_type;
    vk::Format format;
};

Textures createTextures(RenderingState const& core, std::vector<TextureInput> const& paths);
Texture createTexture(RenderingState const& state, std::string const& path, TextureType type, vk::Format format, vk::Sampler sampler);