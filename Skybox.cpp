#include "Skybox.h"

#include "VulkanRenderSystem.h"

#include "TypeLayer.h"
#include "Program.h"
#include "descriptor_set.h"
#include <vulkan/vulkan_enums.hpp>
#include <spdlog/spdlog.h>

#include <stb/stb_image.h>
#include <vulkan/vulkan_structs.hpp>

std::unique_ptr<Program> createSkyBoxProgram(RenderingState const& state, vk::RenderPass render_pass)
{
    layer_types::Program program_desc;
    program_desc.vertex_shader= {{"./shaders/skybox_vert.spv"}};
    program_desc.fragment_shader = {{"./shaders/skybox_frag.spv"}};

    program_desc.buffers.push_back({layer_types::Buffer{
        .name = "World",
        .type = layer_types::BufferType::WorldBufferObject,
        .size = 1,
        .binding = layer_types::Binding{
            .name = {{"binding world"}},
            .binding = 0,
            .type = layer_types::BindingType::Uniform,
            .size = 1,
            .vertex = true,
            .fragment = true
        }
    }});

    program_desc.buffers.push_back({layer_types::Buffer{
        .name = "World",
        .type = layer_types::BufferType::NoBuffer,
        .size = 1,
        .binding = layer_types::Binding{
            .name = {{"Cube Sampler"}},
            .binding = 0,
            .type = layer_types::BindingType::TextureSampler,
            .size = 1,
            .fragment = true,
        }
    }});
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"model_buffer"}},
        .type = layer_types::BufferType::ModelBufferObject,
        .size = 10,
        .binding = layer_types::Binding {
            .name = {{"binding model"}},
            .binding = 0,
            .type = layer_types::BindingType::Storage,
            .size = 1,
            .vertex = true,
            .fragment = true
        }
    }});

    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"model_buffer"}},
        .type = layer_types::BufferType::AtmosphereShaderData,
        .size = 1,
        .binding = layer_types::Binding {
            .name = {{"binding model"}},
            .binding = 0,
            .type = layer_types::BindingType::Uniform,
            .size = 1,
            .vertex = true,
            .fragment = true
        }
    }});

    GraphicsPipelineInput input = createDefaultPipelineInput();
    input.rasterizer_state.setCullMode(vk::CullModeFlagBits::eFront);
    input.depth_stencil.setDepthBoundsTestEnable(false);
    input.depth_stencil.setDepthWriteEnable(false);


    auto program = createProgram(program_desc, state, {}, render_pass, "Render skybox", input);

    return program;
}

std::tuple<vk::Image, vk::DeviceMemory> createCubeboxImage(int width, int height, RenderingState const& state)
{
    vk::ImageCreateInfo image_info;
    image_info.sType = vk::StructureType::eImageCreateInfo;
    image_info.imageType = vk::ImageType::e2D;
    image_info.extent.width = width;
    image_info.extent.height = height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 6;
    image_info.format = vk::Format::eR8G8B8A8Unorm;
    image_info.initialLayout = vk::ImageLayout::eUndefined;
    image_info.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc;
    image_info.flags = vk::ImageCreateFlagBits::eCubeCompatible;

    auto texture_image = state.device.createImage(image_info);
    checkResult(texture_image.result);

    auto mem_reqs = 
                    state.device.getImageMemoryRequirements(texture_image.value);

    auto properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

    vk::MemoryAllocateInfo alloc_info{0};
    alloc_info.sType = vk::StructureType::eMemoryAllocateInfo;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = findMemoryType(state.physical_device, mem_reqs.memoryTypeBits, properties);

    auto texture_image_memory = state.device.allocateMemory(alloc_info);
    checkResult(texture_image_memory.result);

    checkResult(state.device.bindImageMemory(texture_image.value, texture_image_memory.value,0));
    return {texture_image.value, texture_image_memory.value};
}

vk::Image createSkyboxBuffer(RenderingState const& state, std::array<std::string, 6> const& skybox_paths)
{
    int width, height, channels {};

    struct SkyboxData {stbi_uc* pixels; int width; int height; int channels;};
    std::vector<SkyboxData> skybox_data;

    for (auto const& path : skybox_paths)
    {
        SkyboxData s;

        auto pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels)
        {
            spdlog::warn("Could not load skycube texture: {}", path);
        }
        skybox_data.push_back({pixels, width, height, channels});
    }

    vk::DeviceSize image_size = width * height * 4;
    vk::DeviceSize buffer_size = image_size * 6;
    vk::DeviceMemory staging_buffer_memory;

    auto staging_buffer = createBuffer(state, buffer_size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, staging_buffer_memory);
    auto data = state.device.mapMemory(staging_buffer_memory, 0, buffer_size, static_cast<vk::MemoryMapFlagBits>(0));
    checkResult(data.result);

    for (int i = 0; i < skybox_data.size(); ++i)
    {
        memcpy(reinterpret_cast<char*>(data.value)+(image_size*i), skybox_data[i].pixels, image_size);
        stbi_image_free(skybox_data[i].pixels);
    }
    state.device.unmapMemory(staging_buffer_memory);

    auto [image, memory] = createCubeboxImage(width, height, state);
    transitionImageLayout(state, image, vk::Format::eR8G8B8A8Unorm, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 1, 6);


    auto cmd_buffer = beginSingleTimeCommands(state);
    std::vector<vk::BufferImageCopy> regions;
    for (int i = 0; i < 6; ++i)
    {
        vk::BufferImageCopy region{};
        region.bufferOffset = i * image_size;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = i;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = vk::Extent3D{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

        cmd_buffer.copyBufferToImage(staging_buffer, image, vk::ImageLayout::eTransferDstOptimal, region);
    }

    endSingleTimeCommands(state, cmd_buffer);



    state.device.destroyBuffer(staging_buffer);
    state.device.freeMemory(staging_buffer_memory);

    return image;
}

vk::Sampler createCubemapSampler(RenderingState const& state)
{
    vk::SamplerCreateInfo sampler_info{};
    sampler_info.magFilter = vk::Filter::eLinear;
    sampler_info.minFilter = vk::Filter::eLinear;

    sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
    sampler_info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    sampler_info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    sampler_info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    sampler_info.anisotropyEnable = true;
    sampler_info.maxAnisotropy = 16;

    auto sampler = state.device.createSampler(sampler_info);
    checkResult(sampler.result);

    return sampler.value;
}

Skybox createSkybox(RenderingState const& state, vk::RenderPass render_pass, std::array<std::string, 6> skybox_paths)
{
    auto program = createSkyBoxProgram(state, render_pass);
    auto image = createSkyboxBuffer(state, skybox_paths);
    auto image_view = createImageView(state.device, image, vk::Format::eR8G8B8A8Unorm, vk::ImageAspectFlagBits::eColor, 1, vk::ImageViewType::eCube, 6);

    transitionImageLayout(state, image, vk::Format::eR8G8B8A8Unorm, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 1, 6);

    auto sampler = createCubemapSampler(state);
    updateImageSampler(state.device, {image_view}, sampler, program->program.descriptor_sets[1].set, program->program.descriptor_sets[1].layout_bindings[0]);

    return Skybox
    {
        .image = image,
        .view = image_view,
        .program = std::move(program),
        .sampler = sampler
    };
}