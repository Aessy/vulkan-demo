#include "Textures.h"

#include "VulkanRenderSystem.h"

#include <spdlog/spdlog.h>
#include <stb/stb_image.h>

#include <filesystem>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

void generateMipmaps(RenderingState const& state, vk::Image const& image, int32_t width, int32_t height, uint32_t mip_levels)
{
    vk::raii::CommandBuffer cmd_buffer = beginSingleTimeCommands(state);

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

static std::tuple<vk::raii::Image, vk::raii::DeviceMemory> createImageMapTexture(RenderingState const& state, void* pixels, std::size_t width, std::size_t height, vk::Format format)
{
    vk::DeviceSize image_size = width*height*4;
    auto [staging_buffer, staging_buffer_memory] = createBuffer(state, image_size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    auto data = staging_buffer_memory.mapMemory(0, image_size, static_cast<vk::MemoryMapFlagBits>(0));
    memcpy(data, pixels, static_cast<size_t>(image_size));
    staging_buffer_memory.unmapMemory();

    auto [image, image_device_memory] = createImage(state, width, height, 1, format, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, vk::SampleCountFlagBits::e1);

    transitionImageLayout(state, image, format, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 1);
    copyBufferToImage(state, staging_buffer, image, width, height);
    transitionImageLayout(state, image, format, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 1);

    return {std::move(image), std::move(image_device_memory)};
}

static std::tuple<vk::raii::Image, vk::raii::DeviceMemory, uint32_t> createTextureImage(RenderingState const& state, vk::Format format, std::string const& path)
{
    int width, height, channels {};

    //auto pixels = stbi_load("textures/far.jpg", &width, &height, &channels, STBI_rgb_alpha);
    auto pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels)
    {
        spdlog::warn("Could not load texture: {}", path);
    }

    uint32_t mip_levels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

    vk::DeviceSize image_size = width * height * 4;

    auto [staging_buffer, staging_buffer_memory] = createBuffer(state, image_size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    auto data = staging_buffer_memory.mapMemory(0, image_size, static_cast<vk::MemoryMapFlagBits>(0));
    memcpy(data, pixels, static_cast<size_t>(image_size));
    staging_buffer_memory.unmapMemory();
    stbi_image_free(pixels);

    auto [image, image_device_memory] = createImage(state, width, height, mip_levels, format, vk::ImageTiling::eOptimal,
                        vk::ImageUsageFlagBits::eTransferDst
                            | vk::ImageUsageFlagBits::eTransferSrc
                            | vk::ImageUsageFlagBits::eSampled,
                                vk::MemoryPropertyFlagBits::eDeviceLocal,
                                vk::SampleCountFlagBits::e1);

    transitionImageLayout(state, image, format, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mip_levels);
    copyBufferToImage(state, staging_buffer, image, width, height);
    generateMipmaps(state, image, width, height, mip_levels);

    return {std::move(image), std::move(image_device_memory), mip_levels};
}

std::unique_ptr<Texture> createTexture(RenderingState const& state, std::string const& path, TextureType type, vk::Format format, vk::Sampler sampler)
{
    if (type == TextureType::MipMap)
    {
        auto [image, mem, mip_maps] = createTextureImage(state, format, path);
        auto image_view = createTextureImageView(state, image, format, mip_maps);

        auto file_name = std::filesystem::path(path).filename().string();

        return std::make_unique<Texture>(
            std::move(image),
            std::move(mem),
            std::move(image_view),
            sampler,
            file_name);
        
    }
    else
    {
        int width, height, channels{};
        auto pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);

        if (!pixels)
        {
            spdlog::warn("Could not load texture", path);
            return {};
        }

        auto [image, mem] = createImageMapTexture(state, pixels, width,height, format);
        stbi_image_free(pixels);
        auto image_view = createTextureImageView(state, image, format, 1);

        auto file_name = std::filesystem::path(path).filename().string();

        return std::make_unique<Texture>(
            std::move(image),
            std::move(mem),
            std::move(image_view),
            sampler,
            file_name);

    }
}

Textures createTextures(RenderingState const& core, std::vector<TextureInput> const& paths)
{
    Textures textures{.sampler_mip_map = createTextureSampler(core, true),
                      .sampler_no_mip_map = createTextureSampler(core, false),
                      .sampler_depth = createDepthTextureSampler(core)};

    for (auto const& path : paths)
    {
        vk::Sampler sampler = path.texture_type == TextureType::MipMap ? textures.sampler_mip_map: textures.sampler_no_mip_map;
        textures.textures.push_back(createTexture(core, path.path, path.texture_type,
                                            path.format, sampler));
    }

    return textures;
}
