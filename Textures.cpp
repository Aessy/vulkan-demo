#include "Textures.h"

#include "VulkanRenderSystem.h"

#include <stb/stb_image.h>

std::pair<vk::Image, vk::DeviceMemory> createTextureImage(RenderingState const& state, std::string const& path)
{
    int width, height, channels {};

    //auto pixels = stbi_load("textures/far.jpg", &width, &height, &channels, STBI_rgb_alpha);
    auto pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels)
    {
        std::cout << "Could not load texture\n";
    }

    vk::DeviceSize image_size = width * height * 4;
    vk::DeviceMemory staging_buffer_memory;

    auto staging_buffer = createBuffer(state, image_size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, staging_buffer_memory);

    auto data = state.device.mapMemory(staging_buffer_memory, 0, image_size, static_cast<vk::MemoryMapFlagBits>(0));
    memcpy(data.value, pixels, static_cast<size_t>(image_size));
    state.device.unmapMemory(staging_buffer_memory);
    stbi_image_free(pixels);

    auto image = createImage(state, width, height, vk::Format::eR8G8B8A8Srgb, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);

    transitionImageLayout(state, image.first, vk::Format::eR8G8B8A8Srgb, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
    copyBufferToImage(state, staging_buffer, image.first, width, height);
    transitionImageLayout(state, image.first, vk::Format::eR8G8B8A8Srgb, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

    state.device.destroyBuffer(staging_buffer);
    state.device.freeMemory(staging_buffer_memory);

    return image;
}

Texture createTexture(RenderingState const& state, std::string const& path, vk::Sampler sampler)
{
    auto image = createTextureImage(state, path);
    auto image_view = createTextureImageView(state, image.first);

    return Texture {
        .image = image.first,
        .memory = image.second,
        .view = image_view,
        .sampler = sampler
    };
}

Textures createTextures(RenderingState const& core, std::vector<std::string> const& paths)
{
    auto sampler = createTextureSampler(core);

    Textures textures {sampler, {}};
    for (auto const& path : paths)
    {
        textures.textures.push_back(createTexture(core, path, sampler));
    }

    return textures;
}