#pragma once

// For some reason after updating vulkan, X11 defines True and False as macros 
// which vulkan defines as globals... undef after xlib is included
#include "X11/Xlib.h"
#undef True
#undef False

#define VK_USE_PLATFORM_XLIB_KHR
#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_X11
#include "GLFW/glfw3native.h"

#define VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_ASSERT_ON_RESULT
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_funcs.hpp>
#include <vulkan/vulkan_structs.hpp>

#include <memory>
#include <queue>
#include <optional>
#include <iostream>

#include "Model.h"

struct Event
{
    int key{};
    int scancode{};
    int action{};
    int mods{};
};
struct Keyboard
{
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool shift = false;
};

struct CursorPos
{
    uint32_t x{};
    uint32_t y{};
};

struct App
{
    bool window_resize = false;
    std::queue<Event> events;
    Keyboard keyboard{};

    CursorPos cursor_pos{};

    int test = 100;
};

struct QueueFamilyIndices {
    std::optional<unsigned int> graphics_family;
    std::optional<unsigned int> present_family;
};

struct Semaphores
{
    std::vector<vk::Fence> in_flight_fence;
    std::vector<vk::Semaphore> image_available_semaphore;
    std::vector<vk::Semaphore> render_finished_semaphore;
};

struct SwapChain
{
    vk::SwapchainKHR swap_chain;
    std::vector<vk::Image> images;
    vk::Format swap_chain_image_format;
    vk::Extent2D extent;
};

struct DepthResources
{
    vk::Image depth_image;
    vk::DeviceMemory depth_image_memory;

    vk::ImageView depth_image_view;
};

struct UniformBuffer
{
    vk::Buffer uniform_buffers;
    vk::DeviceMemory uniform_device_memory;
    void* uniform_buffers_mapped;
};

template<typename T>
void checkResult(T result)
{
    if (result != vk::Result::eSuccess)
    {
        std::cout << "Result error: " << static_cast<int>(result) << '\n';

    }
}

struct RenderingState
{
    std::unique_ptr<App> app;

    GLFWwindow* window{nullptr};

    vk::Instance instance;
    vk::SurfaceKHR surface;
    vk::Device device;
    QueueFamilyIndices indices;
    vk::PhysicalDevice physical_device;
    vk::RenderPass render_pass;
    std::vector<vk::ImageView> image_views;

    SwapChain swap_chain;
    Semaphores semaphores;

    vk::CommandPool command_pool;
    std::vector<vk::CommandBuffer> command_buffer;
    DepthResources color_resources;
    DepthResources depth_resources;
    std::vector<vk::Framebuffer> framebuffers;

    vk::Queue graphics_queue;
    vk::Queue present_queue;

    uint32_t current_frame{};

    Camera camera;
    vk::SampleCountFlagBits msaa;
    
};

RenderingState createVulkanRenderState();
std::pair<vk::Image, vk::DeviceMemory> createImage(RenderingState const& state, uint32_t width, uint32_t height, uint32_t mip_levels, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties, vk::SampleCountFlagBits num_samples = vk::SampleCountFlagBits::e1);
vk::ImageView createImageView(vk::Device const& device, vk::Image const& image, vk::Format format, vk::ImageAspectFlags aspec_flags, uint32_t mip_levels);

vk::ShaderModule createShaderModule(std::vector<char> const& code, vk::Device const& device);

vk::CommandBuffer beginSingleTimeCommands(RenderingState const& state);;
void endSingleTimeCommands(RenderingState const& state, vk::CommandBuffer const& cmd_buffer);
vk::Buffer createVertexBuffer(RenderingState const& state, std::vector<Vertex> const& vertices);
vk::Buffer createIndexBuffer(RenderingState const& state, std::vector<uint32_t> indices);
void transitionImageLayout(RenderingState const& state, vk::Image const& image, vk::Format const& format, vk::ImageLayout old_layout, vk::ImageLayout new_layout, uint32_t mip_levels);
void copyBufferToImage(RenderingState const& state, vk::Buffer buffer, vk::Image image, uint32_t width, uint32_t height);
vk::ImageView createTextureImageView(RenderingState const& state, vk::Image const& texture_image, vk::Format format, uint32_t mip_levels);
vk::Sampler createTextureSampler(RenderingState const& state);
vk::Buffer createBuffer(RenderingState const& state,
                        vk::DeviceSize size, vk::BufferUsageFlags usage,
                        vk::MemoryPropertyFlags properties, vk::DeviceMemory& buffer_memory);


struct ShaderStage
{
    vk::ShaderModule module;
    vk::ShaderStageFlagBits stage;
};

std::pair<std::vector<vk::Pipeline>, vk::PipelineLayout>  createGraphicsPipline(vk::Device const& device, vk::Extent2D const& swap_chain_extent, vk::RenderPass const& render_pass, std::vector<vk::DescriptorSetLayout> const& desc_set_layout, std::vector<ShaderStage> shader_stages, vk::SampleCountFlagBits msaa);

void recreateSwapchain(RenderingState& state);

template<typename BufferObject>
auto createUniformBuffers(RenderingState const& state)
{
    vk::DeviceSize buffer_size = sizeof(BufferObject);

    std::vector<UniformBuffer> ubos;
    size_t const max_frames_in_flight = 2;
    for (size_t i = 0; i < max_frames_in_flight; ++i)
    {
        vk::DeviceMemory uniform_buffer_memory;
        auto buffer = createBuffer(state, buffer_size, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible
                                                                                 |vk::MemoryPropertyFlagBits::eHostCoherent,
                                                                                 uniform_buffer_memory);
        auto mapped = state.device.mapMemory(uniform_buffer_memory, 0, buffer_size, static_cast<vk::MemoryMapFlagBits>(0));
        checkResult(mapped.result);

        ubos.push_back(UniformBuffer{buffer, uniform_buffer_memory, mapped.value});
    }

    return ubos;
}

template<typename BufferObject>
auto createStorageBuffers(RenderingState const& state, uint32_t size)
{
    vk::DeviceSize buffer_size = sizeof(BufferObject) * size;

    std::vector<UniformBuffer> ubos;
    size_t const max_frames_in_flight = 2;
    for (size_t i = 0; i < max_frames_in_flight; ++i)
    {
        vk::DeviceMemory uniform_buffer_memory;
        auto buffer = createBuffer(state, buffer_size, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible
                                                                                 | vk::MemoryPropertyFlagBits::eHostCoherent,
                                                                                   uniform_buffer_memory);
        auto mapped = state.device.mapMemory(uniform_buffer_memory, 0, buffer_size, static_cast<vk::MemoryMapFlagBits>(0));
        checkResult(mapped.result);

        ubos.push_back(UniformBuffer{buffer, uniform_buffer_memory, mapped.value});
    }

    return ubos;
}

