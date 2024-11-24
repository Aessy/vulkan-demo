#pragma once

#include "VulkanRenderSystem.h"

#include <vector>

struct Texture
{
    vk::raii::Image image;
    vk::raii::DeviceMemory memory;
    vk::raii::ImageView view;
    
    // Weak reference to a sampler
    vk::Sampler sampler;

    std::string const name;

    uint32_t mip_levels;
};

struct Textures
{
    vk::raii::Sampler sampler_mip_map;
    vk::raii::Sampler sampler_no_mip_map;
    std::vector<std::unique_ptr<Texture>> textures;
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

Textures createTextures(RenderingState const& core,
                        std::vector<TextureInput> const& paths);
std::unique_ptr<Texture> createTexture(RenderingState const& state,
                                       std::string const& path,
                                       TextureType type,
                                       vk::Format format,
                                       vk::Sampler sampler);