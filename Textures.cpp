#include "Textures.h"

#include "VulkanRenderSystem.h"

#include <stb/stb_image.h>

#include <filesystem>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

void generateMipmaps(RenderingState const& state, vk::Image const& image, int32_t width, int32_t height, uint32_t mip_levels)
{
    vk::CommandBuffer cmd_buffer = beginSingleTimeCommands(state);

    vk::ImageMemoryBarrier barrier{};
    barrier.sType = vk::StructureType::eImageMemoryBarrier;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mip_width = width;
    int32_t mipHeight = height;

    for (uint32_t i = 1; i < mip_levels; ++i)
    {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

        cmd_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
                        vk::DependencyFlags{0}, 0, {}, barrier);

        vk::ImageBlit blit{};
        blit.srcOffsets[0] = vk::Offset3D{0,0,0};
        blit.srcOffsets[1] = vk::Offset3D{mip_width, mipHeight, 1};
        blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = vk::Offset3D{0,0,0};
        blit.dstOffsets[1] = vk::Offset3D{mip_width > 1 ? mip_width / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
        blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        
        cmd_buffer.blitImage(image, vk::ImageLayout::eTransferSrcOptimal, image, vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eLinear);

        barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        cmd_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                        vk::DependencyFlags{0}, 0, {}, barrier);

        if (mip_width > 1) mip_width /= 2;
        if (mipHeight> 1) mipHeight /= 2;
    }

    barrier.subresourceRange.baseMipLevel = mip_levels - 1;
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    cmd_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                    vk::DependencyFlags{0}, 0, {}, barrier);

    endSingleTimeCommands(state, cmd_buffer);
}

static std::tuple<vk::Image, vk::DeviceMemory, uint32_t> createTextureImage(RenderingState const& state, std::string const& path)
{
    int width, height, channels {};

    //auto pixels = stbi_load("textures/far.jpg", &width, &height, &channels, STBI_rgb_alpha);
    auto pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels)
    {
        std::cout << std::string("Could not load texture") + path + "\n";
    }

    uint32_t mip_levels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

    vk::DeviceSize image_size = width * height * 4;
    vk::DeviceMemory staging_buffer_memory;

    auto staging_buffer = createBuffer(state, image_size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, staging_buffer_memory);

    auto data = state.device.mapMemory(staging_buffer_memory, 0, image_size, static_cast<vk::MemoryMapFlagBits>(0));
    memcpy(data.value, pixels, static_cast<size_t>(image_size));
    state.device.unmapMemory(staging_buffer_memory);
    stbi_image_free(pixels);

    auto image = createImage(state, width, height, mip_levels, vk::Format::eR8G8B8A8Srgb, vk::ImageTiling::eOptimal,
                        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);

    transitionImageLayout(state, image.first, vk::Format::eR8G8B8A8Srgb, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mip_levels);
    copyBufferToImage(state, staging_buffer, image.first, width, height);
    generateMipmaps(state, image.first, width, height, mip_levels);

    state.device.destroyBuffer(staging_buffer);
    state.device.freeMemory(staging_buffer_memory);

    return {image.first, image.second, mip_levels};
}

Texture createTexture(RenderingState const& state, std::string const& path, vk::Sampler sampler)
{
    auto [image, mem, mip_maps] = createTextureImage(state, path);
    auto image_view = createTextureImageView(state, image, mip_maps);

    auto file_name = std::filesystem::path(path).filename();

    return Texture {
        .image = image,
        .memory = mem,
        .view = image_view,
        .sampler = sampler,
        .name = file_name
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
