#pragma once

#ifdef __linux__
#define VK_USE_PLATFORM_XLIB_KHR
#define GLFW_EXPOSE_NATIVE_X11
#elif _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#undef True
#undef False

#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"

#include "GLFW/glfw3native.h"

#define VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_RAII_NO_EXCEPTIONS
#define VULKAN_HPP_ASSERT_ON_RESULT
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

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
    double x{};
    double y{};
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
    std::vector<std::unique_ptr<vk::raii::Fence>> in_flight_fence;
    std::vector<std::unique_ptr<vk::raii::Semaphore>> image_available_semaphore;
    std::vector<std::unique_ptr<vk::raii::Semaphore>> render_finished_semaphore;
};

struct SwapChain
{
    vk::raii::SwapchainKHR swap_chain;
    std::vector<vk::Image> images;
    vk::Format swap_chain_image_format;
    vk::Extent2D extent;
};

struct DepthResources
{
    vk::raii::Image depth_image;
    vk::raii::DeviceMemory device_memory;
    vk::raii::ImageView depth_image_view;
};

struct ImageResource
{
    vk::raii::Image image;
    vk::raii::DeviceMemory device_memory;
    vk::raii::ImageView image_view;
};

struct UniformBuffer
{
    vk::raii::Buffer uniform_buffers;
    vk::raii::DeviceMemory uniform_device_memory;
    void* uniform_buffers_mapped;
    size_t alignment;
};

struct Buffer
{
    vk::raii::Buffer buffer;
    vk::raii::DeviceMemory memory;
};

template<typename T>
void checkResult(T result)
{
    if (result != vk::Result::eSuccess)
    {
        std::cout << "Result error: " << static_cast<int>(result) << '\n';

    }
}

struct FramebufferResources
{
    DepthResources color_resources;
    DepthResources depth_resources;
    DepthResources depth_resolved_resources;
};

struct RenderingState
{
    vk::raii::Context context;
    std::unique_ptr<App> app;

    GLFWwindow* window{nullptr};

    vk::raii::Instance instance;
    vk::raii::SurfaceKHR surface;
    vk::raii::Device device;
    QueueFamilyIndices indices;
    vk::raii::PhysicalDevice physical_device;
    std::vector<vk::raii::ImageView> image_views;

    SwapChain swap_chain;
    Semaphores semaphores;

    vk::raii::CommandPool command_pool;
    std::vector<vk::raii::CommandBuffer> command_buffer;
    
    vk::raii::Queue graphics_queue;
    vk::raii::Queue present_queue;

    uint32_t current_frame{};

    Camera camera;
    vk::SampleCountFlagBits msaa;

    uint32_t uniform_buffer_alignment_min{};
};

struct GraphicsPipelineInput
{
    vk::PipelineRasterizationStateCreateInfo rasterizer_state{};
    vk::PipelineDepthStencilStateCreateInfo depth_stencil{};
};

vk::PipelineRasterizationStateCreateInfo createRasterizerState();
vk::PipelineDepthStencilStateCreateInfo createDepthStencil();

vk::Format getDepthFormat();

GraphicsPipelineInput createDefaultPipelineInput();

std::optional<RenderingState> createVulkanRenderState();

void initImgui(vk::Device const& device, vk::PhysicalDevice const& physical, vk::Instance const& instance,
               vk::Queue const& queue, vk::RenderPass const& render_pass,
               RenderingState const& state, GLFWwindow* window, vk::SampleCountFlagBits msaa);
std::tuple<vk::raii::Image, vk::raii::DeviceMemory> createImage(RenderingState const& state, uint32_t width, uint32_t height, uint32_t mip_levels, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties, vk::SampleCountFlagBits num_samples = vk::SampleCountFlagBits::e1, unsigned int layer_count = 1);
vk::raii::ImageView createImageView(vk::raii::Device const& device, vk::Image const& image, vk::Format format, vk::ImageAspectFlags aspec_flags, uint32_t mip_levels, vk::ImageViewType view_type = vk::ImageViewType::e2D, uint32_t level_count = 1, uint32_t base_level = 0);
uint32_t findMemoryType(vk::PhysicalDevice const& physical_device, uint32_t type_filter, vk::MemoryPropertyFlags properties);

vk::ShaderModule createShaderModule(std::vector<char> const& code, vk::Device const& device);

DepthResources createDepth(RenderingState const& state, vk::SampleCountFlagBits msaa, unsigned int layer_count = 1);
vk::raii::CommandBuffer beginSingleTimeCommands(RenderingState const& state);

void endSingleTimeCommands(RenderingState const& state, vk::CommandBuffer const& cmd_buffer);
Buffer createVertexBuffer(RenderingState const& state, std::vector<Vertex> const& vertices);
Buffer createIndexBuffer(RenderingState const& state, std::vector<uint32_t> indices);
void transitionImageLayout(RenderingState const& state, vk::Image const& image, vk::Format const& format, vk::ImageLayout old_layout, vk::ImageLayout new_layout, uint32_t mip_levels, uint32_t layer_count = 1);
void transitionImageLayout(vk::CommandBuffer const& cmd_buffer, vk::Image const& image, vk::Format const& format, vk::ImageLayout old_layout, vk::ImageLayout new_layout, uint32_t mip_levels, uint32_t layer_count = 1);
void copyBufferToImage(RenderingState const& state, vk::Buffer buffer, vk::Image image, uint32_t width, uint32_t height);
vk::raii::ImageView createTextureImageView(RenderingState const& state, vk::Image const& texture_image, vk::Format format, uint32_t mip_levels, uint32_t level_count = 1);
vk::raii::Sampler createTextureSampler(RenderingState const& state, bool mip_maps);
Buffer createBuffer(RenderingState const& state,
                    vk::DeviceSize size, vk::BufferUsageFlags usage,
                    vk::MemoryPropertyFlags properties);

std::vector<std::unique_ptr<ImageResource>> createFogBuffer(RenderingState const& state, vk::MemoryPropertyFlags properties);

void dispatchPipeline(RenderingState const& state);

struct ShaderStage
{
    vk::ShaderModule module;
    vk::ShaderStageFlagBits stage;
};

std::pair<std::vector<vk::Pipeline>, vk::PipelineLayout>  createGraphicsPipline(vk::Device const& device,
                                                                                vk::Extent2D const& swap_chain_extent,
                                                                                vk::RenderPass const& render_pass,
                                                                                std::vector<vk::DescriptorSetLayout> const& desc_set_layout,
                                                                                std::vector<ShaderStage> shader_stages,
                                                                                vk::SampleCountFlagBits msaa,
                                                                                vk::PolygonMode polygon_mode,
                                                                                GraphicsPipelineInput const& input);

std::pair<std::vector<vk::Pipeline>, vk::PipelineLayout> createComputePipeline(vk::Device const& device, ShaderStage const& compute_stage, std::vector<vk::DescriptorSetLayout> const& desc_set_layouts);

template<typename BufferObject>
auto createUniformBuffers(RenderingState const& state, int count = 1, std::size_t alignment = 1)
{

    vk::DeviceSize object_size = sizeof(BufferObject);
    vk::DeviceSize element_size = count > 1 ? alignment - ((object_size - 1) % alignment) + (object_size-1)
                                            : object_size;

    vk::DeviceSize buffer_size = element_size * count;

    std::vector<std::unique_ptr<UniformBuffer>> ubos;
    size_t const max_frames_in_flight = 2;
    for (size_t i = 0; i < max_frames_in_flight; ++i)
    {
        auto [buffer, uniform_buffer_memory] = createBuffer(state, buffer_size, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible
                                                                                 |vk::MemoryPropertyFlagBits::eHostCoherent);
        auto mapped = uniform_buffer_memory.mapMemory(0, buffer_size);

        ubos.push_back(std::make_unique<UniformBuffer>(std::move(buffer), std::move(uniform_buffer_memory), mapped, alignment));
    }

    return ubos;
}

DepthResources createColorResources(RenderingState const& state, vk::Format format);

template<typename BufferObject>
auto createStorageBuffers(RenderingState const& state, uint32_t size)
{
    vk::DeviceSize buffer_size = sizeof(BufferObject) * size;

    std::vector<std::unique_ptr<UniformBuffer>> ubos;
    size_t const max_frames_in_flight = 2;
    for (size_t i = 0; i < max_frames_in_flight; ++i)
    {
        auto [buffer, uniform_buffer_memory] = createBuffer(state, buffer_size, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible
                                                                                 | vk::MemoryPropertyFlagBits::eHostCoherent);

        auto mapped = uniform_buffer_memory.mapMemory(0, buffer_size);

        ubos.push_back(std::make_unique<UniformBuffer>(std::move(buffer), std::move(uniform_buffer_memory), mapped));
    }

    return ubos;
}

template<typename BufferObject>
inline void writeBuffer(UniformBuffer& dst, BufferObject const& src, size_t index = 0, size_t alignment = 1)
{
    vk::DeviceSize object_size = sizeof(BufferObject);

    vk::DeviceSize element_size = alignment - ((object_size - 1) % alignment) + (object_size-1);
    std::size_t position = element_size * index;

    void* buffer = (unsigned char*)dst.uniform_buffers_mapped+position;
    memcpy(buffer, (unsigned char*)&src, sizeof(BufferObject));
}

template<typename Vertex>
static constexpr vk::VertexInputBindingDescription getBindingDescription()
{
    auto desc = vk::VertexInputBindingDescription{};
    desc.binding = 0;
    desc.stride = sizeof(Vertex);
    desc.inputRate = vk::VertexInputRate::eVertex;

    return desc;
};

template<typename Vertex>
static constexpr std::array<vk::VertexInputAttributeDescription, 6> getAttributeDescriptions()
{
    std::array<vk::VertexInputAttributeDescription, 6> desc;

    desc[0].binding = 0;
    desc[0].location = 0;
    desc[0].format = vk::Format::eR32G32B32Sfloat;
    desc[0].offset = offsetof(Vertex, pos);

    desc[1].binding = 0;
    desc[1].location = 1;
    desc[1].format = vk::Format::eR32G32Sfloat;
    desc[1].offset = offsetof(Vertex, tex_coord);

    desc[2].binding = 0;
    desc[2].location = 2;
    desc[2].format = vk::Format::eR32G32B32Sfloat;
    desc[2].offset = offsetof(Vertex, normal);

    desc[3].binding = 0;
    desc[3].location = 3;
    desc[3].format = vk::Format::eR32G32Sfloat;
    desc[3].offset = offsetof(Vertex, normal_coord);

    desc[4].binding = 0;
    desc[4].location = 4;
    desc[4].format = vk::Format::eR32G32B32Sfloat;
    desc[4].offset = offsetof(Vertex, tangent);

    desc[5].binding = 0;
    desc[5].location = 5;
    desc[5].format = vk::Format::eR32G32B32Sfloat;
    desc[5].offset = offsetof(Vertex, bitangent);

    return  desc;
}
