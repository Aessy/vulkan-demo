#include "VulkanRenderSystem.h"
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "glm/gtx/string_cast.hpp"
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"

#include <algorithm>
#include <iostream>
#include <set>

struct SwapChainSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> present_modes;
};

static uint32_t findMemoryType(vk::PhysicalDevice const& physical_device, uint32_t type_filter, vk::MemoryPropertyFlags properties)
{
    auto mem_properties = physical_device.getMemoryProperties();

    for (uint32_t i = 0; i < mem_properties.memoryTypes.size(); ++i)
    {
        if (type_filter & (1 << i) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    std::cout << "Missing memory type\n";
    return 0;
}


static void framebufferResizeCallback(GLFWwindow* window, int, int)
{
    auto app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));

    std::cout << app->test << '\n';
    app->window_resize = true;
}

static void keyPressedCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    auto app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
    app->events.push(Event{.key=key,.scancode=scancode,.action=action,.mods=mods});
}

static void cursorMovedCallback(GLFWwindow* window, double x_pos, double y_pos)
{
    auto app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));

    app->cursor_pos = CursorPos{uint32_t(x_pos), uint32_t(y_pos)};
}

GLFWwindow* setupGlfw(App& app)
{
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);


    auto window = glfwCreateWindow(800, 600, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(window, &app);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    glfwSetKeyCallback(window, keyPressedCallback);
    glfwSetCursorPosCallback(window, cursorMovedCallback);


    return window;
}

bool checkValidationLayerSupport(std::vector<const char*> const& validation_layers)
{
    auto layers = vk::enumerateInstanceLayerProperties().value;

    for (const char* name : validation_layers)
    {
        bool layer_found = false;

        for (auto const& layer_properties : layers)
        {
            std::cout << "Layer property: " << layer_properties.layerName << '\n';;
            if (strcmp(name, layer_properties.layerName)  == 0)
            {
                layer_found = true;
                break;
            }
        }

        if (!layer_found) {
            return false;
        }
    }

    return true;
}


static auto createInstance(bool validation_layers_on)
{
    std::vector<const char*> const validation_layers
    {
        "VK_LAYER_KHRONOS_validation"
    };

    bool use_validation_layer = validation_layers_on && checkValidationLayerSupport(validation_layers);
    if (use_validation_layer)
    {
        std::cout << "Using validation layers\n";
    }

    vk::ApplicationInfo app_info{};

    app_info.sType = vk::StructureType::eApplicationInfo;
    app_info.pApplicationName = "Hello triangle";
    app_info.applicationVersion = VK_MAKE_VERSION(1,0,0);
    app_info.pEngineName = "No Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1,0,0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    vk::InstanceCreateInfo info{};
    info.sType = vk::StructureType::eInstanceCreateInfo;
    info.pApplicationInfo = &app_info;
    
    uint32_t glfw_extensions_count = 0;
    auto glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extensions_count);

    info.enabledExtensionCount = glfw_extensions_count;
    info.ppEnabledExtensionNames = glfw_extensions;
    if (use_validation_layer)
    {
        info.setEnabledLayerCount(validation_layers.size());
        info.setPpEnabledLayerNames(validation_layers.data());
    }
    else
    {
        info.enabledLayerCount = 0;
    }

    std::cout << "Extension count: " << glfw_extensions_count << '\n';
    std::cout << "Extensaions " << *glfw_extensions << '\n';

    return vk::createInstance(info, nullptr).value;
}

static vk::SurfaceKHR createSurface(vk::Instance const& instance, GLFWwindow* window)
{
    vk::SurfaceKHR surface;
    glfwCreateWindowSurface(instance, window, nullptr, reinterpret_cast<VkSurfaceKHR_T**>(&surface));

    return surface;
}

static int scoreDevice(vk::PhysicalDevice const& device)
{
    auto family_properties = device.getQueueFamilyProperties();
    auto const properties = device.getProperties();

    int score = 0;
    if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
    {
        score += 1000;
    }

    score += properties.limits.maxImageDimension2D;

    auto const device_features = device.getFeatures();
    
    if (!device_features.geometryShader)
    {
        return 0;
    }

    std::cout << "Score: " << score << '\n';

    return score;
}


static vk::PhysicalDevice createPhysicalDevice(vk::Instance const& instance)
{
    auto devices = instance.enumeratePhysicalDevices().value;
    if (devices.empty())
    {
        std::cout << "No GPU with vulkan support\n";
        return {};
    }

    std::sort(devices.begin(), devices.end(), [](auto const& lhs, auto const& rhs) {return scoreDevice(lhs) < scoreDevice(rhs);});

    std::cout << "GPU count: " << devices.size() << '\n';
    vk::PhysicalDevice physical_device = devices.back();
    return physical_device;
}

static QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice const& physical_device, vk::SurfaceKHR const& surface)
{
    QueueFamilyIndices indices {{}};

    auto queue_families = physical_device.getQueueFamilyProperties();

    int i = 0;
    for (auto const& queue_family : queue_families)
    {
        if (queue_family.queueFlags & vk::QueueFlagBits::eGraphics)
        {
            indices.graphics_family = i;
            std::cout << "Found graphics queue: " << i << '\n';
        }

        vk::Bool32 present_support = physical_device.getSurfaceSupportKHR(i, surface).value;

        if (present_support)
        {
            indices.present_family = i;
            std::cout << "Found present_queue: " << i << '\n';
        }

        if (indices.graphics_family && indices.present_family)
        {
            break;
        }

        i++;
    }

    indices.graphics_family = i;
    return indices;
    
}

static bool isDeviceSuitable(vk::PhysicalDevice const& device, vk::SurfaceKHR const& surface)
{
    return true;
}

static vk::Device createLogicalDevice(vk::PhysicalDevice const& physical_device, QueueFamilyIndices const& indices)
{
    //
    // Local devices
    //


    auto result = physical_device.enumerateDeviceExtensionProperties();
    for (auto const& extension : result.value)
    {
        std::cout << "Extension: " << extension.extensionName << '\n';
    }
    float queue_priority = 1.0f;

    vk::DeviceQueueCreateInfo queue_create_info{};
    queue_create_info.sType = vk::StructureType::eDeviceQueueCreateInfo;
    queue_create_info.queueFamilyIndex = *indices.graphics_family;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;

    std::set<unsigned int> unique_queue_families = { *indices.graphics_family, *indices.present_family };

    std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;

    for (auto const queue_family : unique_queue_families)
    {
        vk::DeviceQueueCreateInfo queue_create_info;
        queue_create_info.sType = vk::StructureType::eDeviceQueueCreateInfo;
        queue_create_info.queueFamilyIndex = queue_family;
        queue_create_info.queueCount = 1;
        queue_create_info.pQueuePriorities = &queue_priority;
        queue_create_infos.push_back(queue_create_info);
    }

    vk::PhysicalDeviceFeatures device_features{};
    device_features.samplerAnisotropy = true;
    
    vk::PhysicalDeviceVulkan11Features f{};
    f.shaderDrawParameters = true;

    vk::PhysicalDeviceDescriptorIndexingFeatures desc_indexing_features {};
    desc_indexing_features.sType = vk::StructureType::ePhysicalDeviceDescriptorIndexingFeatures;
    desc_indexing_features.shaderSampledImageArrayNonUniformIndexing = true;
    desc_indexing_features.runtimeDescriptorArray = true;
    desc_indexing_features.descriptorBindingVariableDescriptorCount = true;
    desc_indexing_features.descriptorBindingPartiallyBound = true;

    desc_indexing_features.pNext = &f;

    static const std::vector<const char*> device_extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME
    };

    vk::DeviceCreateInfo device_info;
    device_info.enabledExtensionCount = device_extensions.size();
    device_info.setPpEnabledExtensionNames(device_extensions.data());
    device_info.enabledLayerCount = 0;

    device_info.sType = vk::StructureType::eDeviceCreateInfo;
    device_info.pQueueCreateInfos = &queue_create_info;
    device_info.queueCreateInfoCount = queue_create_infos.size();
    device_info.pEnabledFeatures = &device_features;
    device_info.pNext = &desc_indexing_features;

    auto device = physical_device.createDevice(device_info, nullptr);
    checkResult(device.result);
    return device.value;
}

static SwapChainSupportDetails querySwapChainSupport(vk::PhysicalDevice const& device, vk::SurfaceKHR const& surface)
{
    SwapChainSupportDetails details;

    details.capabilities = device.getSurfaceCapabilitiesKHR(surface).value;
    details.formats = device.getSurfaceFormatsKHR(surface).value;
    details.present_modes = device.getSurfacePresentModesKHR(surface).value;

    return details;
}

vk::SurfaceFormatKHR chooseSwapSurfaceFoprmat(std::vector<vk::SurfaceFormatKHR> const& available_formats)
{
    for (auto const& available_format : available_formats)
    {
        if (available_format.format == vk::Format::eB8G8R8A8Srgb && available_format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
        {
            return available_format;
        }
    }

    return available_formats[0];
}

vk::PresentModeKHR chooseSwapPresentMode(std::vector<vk::PresentModeKHR>const& present_modes)
{
    for (auto const& available_present_mode : present_modes)
    {
        std::cout << "Present mode: " << vk::to_string(available_present_mode) << '\n';
        if (available_present_mode == vk::PresentModeKHR::eMailbox)
        {
            return available_present_mode;
        }
    }

    std::cout << "Using fifo present mode\n";
    return vk::PresentModeKHR::eImmediate;
}

vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities, GLFWwindow* window)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        std::cout << "extent from Capabilities\n";
        return capabilities.currentExtent;
    }
    else
    {
        int width {};
        int height {};
        glfwGetFramebufferSize(window, &width, &height);
        return vk::Extent2D { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
    }
}


static SwapChain createSwapchain(vk::PhysicalDevice const& physical_device, vk::Device const& device, vk::SurfaceKHR const& surface, GLFWwindow* window, QueueFamilyIndices const& indices)
{
    auto swap_chain_support = querySwapChainSupport(physical_device, surface);

    auto surface_format = chooseSwapSurfaceFoprmat(swap_chain_support.formats);
    auto present_mode = chooseSwapPresentMode(swap_chain_support.present_modes);
    auto extent = chooseSwapExtent(swap_chain_support.capabilities, window);

    uint32_t image_count = swap_chain_support.capabilities.minImageCount + 1;

    if (swap_chain_support.capabilities.maxImageCount > 0 && image_count > swap_chain_support.capabilities.maxImageCount)
    {
        image_count = swap_chain_support.capabilities.maxImageCount;
    }

    vk::SwapchainCreateInfoKHR swap_chain_create_info;

    swap_chain_create_info.sType = vk::StructureType::eSwapchainCreateInfoKHR;
    swap_chain_create_info.setSurface(surface);
    swap_chain_create_info.setMinImageCount(image_count);
    swap_chain_create_info.setImageFormat(surface_format.format);
    swap_chain_create_info.setImageColorSpace(surface_format.colorSpace);
    swap_chain_create_info.setImageExtent(extent);
    swap_chain_create_info.setImageArrayLayers(1);
    swap_chain_create_info.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment);

    uint32_t queue_family_indices[] = {indices.graphics_family.value(), indices.present_family.value()};

    if (indices.graphics_family != indices.present_family)
    {
        swap_chain_create_info.setImageSharingMode(vk::SharingMode::eConcurrent);
        swap_chain_create_info.setQueueFamilyIndexCount(2);
        swap_chain_create_info.setPQueueFamilyIndices(queue_family_indices);
    }
    else
    {
        swap_chain_create_info.setImageSharingMode(vk::SharingMode::eExclusive);
        swap_chain_create_info.setQueueFamilyIndexCount(0);
        swap_chain_create_info.setPQueueFamilyIndices(nullptr);
    }

    swap_chain_create_info.setPreTransform(swap_chain_support.capabilities.currentTransform);
    swap_chain_create_info.setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque);
    swap_chain_create_info.setPresentMode(present_mode);
    swap_chain_create_info.setClipped(true);
    // swap_chain_create_info.setOldSwapchain(VK_NULL_HANDLE);

    auto swap_chain = device.createSwapchainKHR(swap_chain_create_info).value;
    auto swap_chain_images = device.getSwapchainImagesKHR(swap_chain);
    auto swap_chain_image_format = surface_format.format;
    auto swap_chain_extent = extent;

    SwapChain swap_chain_return {
        swap_chain,
        swap_chain_images.value,
        swap_chain_image_format,
        swap_chain_extent
    };

    return swap_chain_return;
}

static auto createImageViews(SwapChain const& sc, vk::Device const& device)
{
    std::vector<vk::ImageView> swap_chain_image_views;

    for (uint32_t i = 0; i < sc.images.size(); ++i)
    {
        vk::ImageViewCreateInfo image_view_create_info {};
        image_view_create_info.sType = vk::StructureType::eImageViewCreateInfo;
        image_view_create_info.setImage(sc.images[i]);

        image_view_create_info.setViewType(vk::ImageViewType::e2D);
        image_view_create_info.setFormat(sc.swap_chain_image_format);

        image_view_create_info.components.r = vk::ComponentSwizzle::eIdentity;
        image_view_create_info.components.g = vk::ComponentSwizzle::eIdentity;
        image_view_create_info.components.b = vk::ComponentSwizzle::eIdentity;
        image_view_create_info.components.a = vk::ComponentSwizzle::eIdentity;

        image_view_create_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        image_view_create_info.subresourceRange.baseMipLevel = 0;
        image_view_create_info.subresourceRange.levelCount= 1;
        image_view_create_info.subresourceRange.baseArrayLayer = 0;
        image_view_create_info.subresourceRange.layerCount = 1;

        swap_chain_image_views.push_back(device.createImageView(image_view_create_info).value);
    }

    return swap_chain_image_views;
}

static vk::Format getDepthFormat()
{
    vk::Format format = vk::Format::eD32Sfloat;
    return format;
}

static vk::RenderPass createRenderPass(vk::Device const& device, vk::Format const& swap_chain_image_format)
{
    vk::AttachmentDescription color_attachment{};
    color_attachment.format = swap_chain_image_format;
    color_attachment.samples = vk::SampleCountFlagBits::e1;
    color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
    color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
    color_attachment.stencilLoadOp =  vk::AttachmentLoadOp::eDontCare;
    color_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    color_attachment.initialLayout = vk::ImageLayout::eUndefined;
    color_attachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

    vk::AttachmentDescription depth_attachment{};
    depth_attachment.format = getDepthFormat();
    depth_attachment.samples = vk::SampleCountFlagBits::e1;
    depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
    depth_attachment.storeOp = vk::AttachmentStoreOp::eDontCare;
    depth_attachment.stencilLoadOp =  vk::AttachmentLoadOp::eDontCare;
    depth_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depth_attachment.initialLayout = vk::ImageLayout::eUndefined;
    depth_attachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::AttachmentReference color_attachment_ref;
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::AttachmentReference depth_attachment_ref;
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::SubpassDescription subpass {};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.setColorAttachments(color_attachment_ref);
    subpass.setPDepthStencilAttachment(&depth_attachment_ref);

    vk::SubpassDependency dependency{};
    dependency.setSrcSubpass(VK_SUBPASS_EXTERNAL);
    dependency.setDstSubpass(0);

    dependency.setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests);
    dependency.setSrcAccessMask(static_cast<vk::AccessFlags>(0));

    dependency.setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests);
    dependency.setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite);

    std::array<vk::AttachmentDescription, 2> attachments {color_attachment, depth_attachment};

    vk::RenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = vk::StructureType::eRenderPassCreateInfo;
    render_pass_info.attachmentCount = attachments.size();
    render_pass_info.setAttachments(attachments);
    render_pass_info.subpassCount = 1;
    render_pass_info.setSubpasses(subpass);
    render_pass_info.setDependencies(dependency);

    return device.createRenderPass(render_pass_info).value;
}

static vk::CommandPool createCommandPool(vk::Device const& device, QueueFamilyIndices const& queue_family_indices)
{
    vk::CommandPoolCreateInfo pool_info;
    pool_info.sType = vk::StructureType::eCommandPoolCreateInfo;
    pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    pool_info.queueFamilyIndex = queue_family_indices.graphics_family.value();

    return device.createCommandPool(pool_info).value;

}

static std::vector<vk::CommandBuffer> createCommandBuffer(vk::Device const& device, vk::CommandPool const& command_pool)
{
    vk::CommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = vk::StructureType::eCommandBufferAllocateInfo;
    alloc_info.commandPool = command_pool;
    alloc_info.level = vk::CommandBufferLevel::ePrimary;
    alloc_info.commandBufferCount = 2;

    auto command_buffers = device.allocateCommandBuffers(alloc_info);

    std::cout << "Command buffer size: " << command_buffers.value.size() << '\n';
    return command_buffers.value;
}

static Semaphores createSemaphores(vk::Device const& device)
{
    vk::SemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = vk::StructureType::eSemaphoreCreateInfo;

    vk::FenceCreateInfo fence_info{};
    fence_info.sType = vk::StructureType::eFenceCreateInfo;
    fence_info.flags = vk::FenceCreateFlagBits::eSignaled;

    Semaphores semaphores{};
    for (int i = 0; i < 2; ++i)
    {
        semaphores.in_flight_fence.push_back(device.createFence(fence_info).value);
        semaphores.image_available_semaphore.push_back(device.createSemaphore(semaphore_info).value);
        semaphores.render_finished_semaphore.push_back(device.createSemaphore(semaphore_info).value);

    }

    return semaphores;
}

DepthResources createDepth(RenderingState const& state)
{
    vk::Format format = getDepthFormat();

    auto depth_image = createImage(state, state.swap_chain.extent.width, state.swap_chain.extent.height, format,
                            vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment,
                            vk::MemoryPropertyFlagBits::eDeviceLocal);

    auto depth_image_view = createImageView(state.device, depth_image.first, format, vk::ImageAspectFlagBits::eDepth);

    return {depth_image.first, depth_image.second, depth_image_view};
}

std::pair<vk::Image, vk::DeviceMemory> createImage(RenderingState const& state, uint32_t width, uint32_t height, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties)
{
    vk::ImageCreateInfo image_info;
    image_info.sType = vk::StructureType::eImageCreateInfo;
    image_info.imageType = vk::ImageType::e2D;
    image_info.extent.width = width;
    image_info.extent.height = height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = format;
    image_info.tiling = tiling;
    image_info.initialLayout = vk::ImageLayout::eUndefined;
    image_info.usage = usage;
    image_info.sharingMode = vk::SharingMode::eExclusive;
    image_info.samples = vk::SampleCountFlagBits::e1;
    image_info.flags = static_cast<vk::ImageCreateFlagBits>(0);

    auto texture_image = state.device.createImage(image_info);

    auto mem_reqs = state.device.getImageMemoryRequirements(texture_image.value);

    vk::MemoryAllocateInfo alloc_info{0};
    alloc_info.sType = vk::StructureType::eMemoryAllocateInfo;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = findMemoryType(state.physical_device, mem_reqs.memoryTypeBits, properties);

    auto texture_image_memory = state.device.allocateMemory(alloc_info);

    checkResult(state.device.bindImageMemory(texture_image.value, texture_image_memory.value,0));

    return std::make_pair(texture_image.value, texture_image_memory.value);
}

vk::ImageView createImageView(vk::Device const& device, vk::Image const& image, vk::Format format, vk::ImageAspectFlags aspec_flags)
{
    vk::ImageViewCreateInfo view_info;
    view_info.sType = vk::StructureType::eImageViewCreateInfo;
    view_info.image = image;
    view_info.viewType = vk::ImageViewType::e2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = aspec_flags;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    auto texture_image_view = device.createImageView(view_info);
    checkResult(texture_image_view.result);
    return texture_image_view.value;
}

static std::vector<vk::Framebuffer> createFrameBuffers(vk::Device const& device, std::vector<vk::ImageView> const& swap_chain_image_views, vk::RenderPass const& render_pass, vk::Extent2D const& swap_chain_extent, DepthResources const& depth_resources)
{
    std::vector<vk::Framebuffer> swap_chain_frame_buffers;

    for (auto const& swap_chain_image_view : swap_chain_image_views)
    {
        std::array<vk::ImageView, 2> attachments = {swap_chain_image_view, depth_resources.depth_image_view};

        vk::FramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = vk::StructureType::eFramebufferCreateInfo;
        framebuffer_info.setRenderPass(render_pass);
        framebuffer_info.attachmentCount = attachments.size();
        framebuffer_info.setAttachments(attachments);
        framebuffer_info.width = swap_chain_extent.width;
        framebuffer_info.height = swap_chain_extent.height;
        framebuffer_info.layers = 1;

        swap_chain_frame_buffers.push_back(device.createFramebuffer(framebuffer_info).value);
    }

    return swap_chain_frame_buffers;
}

void initImgui(vk::Device const& device, vk::PhysicalDevice const& physical, vk::Instance const& instance,
               vk::Queue const& queue, vk::RenderPass const& render_pass,
               RenderingState& state, GLFWwindow* window)
{
    std::vector<vk::DescriptorPoolSize> pool_sizes
    {
        { vk::DescriptorType::eSampler, 1000 }
       ,{ vk::DescriptorType::eCombinedImageSampler, 1000 }
       ,{ vk::DescriptorType::eSampledImage, 1000 }
       ,{ vk::DescriptorType::eStorageImage, 1000 }
       ,{ vk::DescriptorType::eUniformTexelBuffer, 1000 }
       ,{ vk::DescriptorType::eStorageTexelBuffer, 1000 }
       ,{ vk::DescriptorType::eUniformBuffer, 1000 }
       ,{ vk::DescriptorType::eStorageBuffer, 1000 }
       ,{ vk::DescriptorType::eUniformBufferDynamic, 1000 }
       ,{ vk::DescriptorType::eStorageBufferDynamic, 1000 }
       ,{ vk::DescriptorType::eInputAttachment, 1000 }
    };

    vk::DescriptorPoolCreateInfo pool_info;
    pool_info.sType = vk::StructureType::eDescriptorPoolCreateInfo;
    pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = std::size(pool_sizes);
    pool_info.setPoolSizes(pool_sizes);

    vk::DescriptorPool imgui_pool = device.createDescriptorPool(pool_info).value;

    ImGui::CreateContext();

    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.Instance = instance;
    init_info.Device = device;
    init_info.DescriptorPool = imgui_pool;
    init_info.PhysicalDevice = physical;
    init_info.Queue = queue;
    init_info.MinImageCount = 3;
    init_info.ImageCount = 3;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&init_info, render_pass);

    auto cmd = beginSingleTimeCommands(state);
    ImGui_ImplVulkan_CreateFontsTexture(cmd);
    endSingleTimeCommands(state, cmd);

    ImGui_ImplVulkan_DestroyFontUploadObjects();
}

RenderingState createVulkanRenderState()
{
    auto app = std::make_unique<App>();


    auto window = setupGlfw(*app);

    auto const instance = createInstance(true);

    auto const surface = createSurface(instance, window);

    auto const physical_device = createPhysicalDevice(instance);

    auto const indices = findQueueFamilies(physical_device, surface);
    auto const device = createLogicalDevice(physical_device, indices);

    auto sc = createSwapchain(physical_device, device, surface, window, indices);

    auto swap_chain_image_views = createImageViews(sc, device);

    auto const render_pass = createRenderPass(device, sc.swap_chain_image_format);

    auto const command_pool = createCommandPool(device, indices);
    auto const command_buffers = createCommandBuffer(device, command_pool);

    auto const semaphores = createSemaphores(device);

    auto const graphics_queue = device.getQueue(*indices.graphics_family, 0);
    auto const present_queue = device.getQueue(*indices.present_family, 0);


    Camera camera;
    camera.proj = glm::perspective(glm::radians(45.0f), sc.extent.width / (float)sc.extent.height, 0.1f, 1000.0f);
    camera.camera_front = glm::vec3(0,0,-1);
    camera.pitch_yawn = glm::vec2(-90, 0);
    camera.up = glm::vec3(0,1,0);
    camera.pos = glm::vec3(0,1,0);

    RenderingState render_state {
        .app = std::move(app),
        .window = window,
        .instance = instance,
        .surface = surface,
        .device = device,
        .indices = indices,
        .physical_device = physical_device,
        .render_pass = render_pass,
        .image_views = swap_chain_image_views,
        .swap_chain = sc,
        .semaphores = semaphores,
        .command_pool = command_pool,
        .command_buffer = command_buffers,
        .graphics_queue = graphics_queue,
        .present_queue = present_queue,
        .camera = camera
    };

    initImgui(device, physical_device, instance, graphics_queue, render_pass, render_state, window);

    auto depth_image = createDepth(render_state);
    auto swap_chain_framebuffers = createFrameBuffers(device, swap_chain_image_views, render_pass, sc.extent, depth_image);

    render_state.framebuffers = swap_chain_framebuffers;
    render_state.depth_resources = depth_image;

    return render_state;
}


vk::CommandBuffer beginSingleTimeCommands(RenderingState const& state)
{
    vk::CommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = vk::StructureType::eCommandBufferAllocateInfo;
    alloc_info.level = vk::CommandBufferLevel::ePrimary;
    alloc_info.commandPool = state.command_pool;
    alloc_info.commandBufferCount = 1;

    auto cmd_buffer = state.device.allocateCommandBuffers(alloc_info).value[0];

    vk::CommandBufferBeginInfo begin_info{};
    begin_info.sType = vk::StructureType::eCommandBufferBeginInfo;
    begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    checkResult(cmd_buffer.begin(begin_info));

    return cmd_buffer;
}

void endSingleTimeCommands(RenderingState const& state, vk::CommandBuffer const& cmd_buffer)
{
    checkResult(cmd_buffer.end());

    vk::SubmitInfo submit_info{};
    submit_info.sType = vk::StructureType::eSubmitInfo;
    submit_info.commandBufferCount = 1;
    submit_info.setCommandBuffers(cmd_buffer);

    checkResult(state.graphics_queue.submit(submit_info));

    checkResult(state.graphics_queue.waitIdle());

    state.device.freeCommandBuffers(state.command_pool, cmd_buffer);
}

static void copyBuffer(RenderingState const& state, vk::Buffer src_buffer, vk::Buffer dst_buffer, vk::DeviceSize size)
{
    auto cmd_buffer = beginSingleTimeCommands(state);

    vk::BufferCopy copy_region{};
    copy_region.size = size;
    cmd_buffer.copyBuffer(src_buffer, dst_buffer, 1, &copy_region);

    endSingleTimeCommands(state, cmd_buffer);
}

void transitionImageLayout(RenderingState const& state, vk::Image const& image, vk::Format const& format, vk::ImageLayout old_layout, vk::ImageLayout new_layout)
{
    auto cmd_buffer = beginSingleTimeCommands(state);

    vk::ImageMemoryBarrier barrier{};
    barrier.sType = vk::StructureType::eImageMemoryBarrier;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = 0;
    barrier.dstQueueFamilyIndex = 0;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = static_cast<vk::AccessFlagBits>(0); // TODO
    barrier.dstAccessMask = static_cast<vk::AccessFlagBits>(0); // TODO
                                                                //
    vk::PipelineStageFlagBits src_stage;
    vk::PipelineStageFlagBits dest_stage;

    if (old_layout == vk::ImageLayout::eUndefined && new_layout == vk::ImageLayout::eTransferDstOptimal)
    {
        barrier.srcAccessMask = vk::AccessFlags{};
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        src_stage = vk::PipelineStageFlagBits::eTopOfPipe;
        dest_stage = vk::PipelineStageFlagBits::eTransfer;
    }
    else if (old_layout == vk::ImageLayout::eTransferDstOptimal && new_layout == vk::ImageLayout::eShaderReadOnlyOptimal)
    {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        src_stage = vk::PipelineStageFlagBits::eTransfer;
        dest_stage = vk::PipelineStageFlagBits::eFragmentShader;
    }
    else
    {
        std::cout << "Error transition image\n";
        return;
    }

    cmd_buffer.pipelineBarrier(src_stage, dest_stage, vk::DependencyFlags{}, {}, {}, barrier);

    endSingleTimeCommands(state, cmd_buffer);
}

void copyBufferToImage(RenderingState const& state, vk::Buffer buffer, vk::Image image, uint32_t width, uint32_t height)
{
    auto cmd_buffer = beginSingleTimeCommands(state);

    vk::BufferImageCopy region {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = vk::Offset3D(0,0,0);
    region.imageExtent = vk::Extent3D(width,height,1);

    cmd_buffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, region);

    endSingleTimeCommands(state, cmd_buffer);
}

vk::Buffer createBuffer(RenderingState const& state,
                        vk::DeviceSize size, vk::BufferUsageFlags usage,
                        vk::MemoryPropertyFlags properties, vk::DeviceMemory& buffer_memory)
{
    vk::BufferCreateInfo buffer_info{};
    buffer_info.sType = vk::StructureType::eBufferCreateInfo;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = vk::SharingMode::eExclusive;

    auto vertex_buffer =  state.device.createBuffer(buffer_info);
    checkResult(vertex_buffer.result);

    auto buffer = vertex_buffer.value;

    auto mem_reqs = state.device.getBufferMemoryRequirements(buffer);

    vk::MemoryAllocateInfo alloc_info{};
    alloc_info.sType = vk::StructureType::eMemoryAllocateInfo;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.setMemoryTypeIndex(findMemoryType(state.physical_device, mem_reqs.memoryTypeBits, properties));

    buffer_memory = state.device.allocateMemory(alloc_info).value;

    checkResult(state.device.bindBufferMemory(buffer, buffer_memory, 0));

    return buffer;
}

vk::ImageView createTextureImageView(RenderingState const& state, vk::Image const& texture_image)
{
    auto texture_image_view = createImageView(state.device, texture_image, vk::Format::eR8G8B8A8Srgb, vk::ImageAspectFlagBits::eColor);
    return texture_image_view;
}

vk::Sampler createTextureSampler(RenderingState const& state)
{
    vk::SamplerCreateInfo sampler_info;
    sampler_info.sType = vk::StructureType::eSamplerCreateInfo;
    sampler_info.magFilter = vk::Filter::eLinear;
    sampler_info.minFilter = vk::Filter::eLinear;
    sampler_info.addressModeU = vk::SamplerAddressMode::eRepeat;
    sampler_info.addressModeV = vk::SamplerAddressMode::eRepeat;
    sampler_info.addressModeW = vk::SamplerAddressMode::eRepeat;
    
    sampler_info.anisotropyEnable = true;

    auto properties = state.physical_device.getProperties();

    sampler_info.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    sampler_info.borderColor = vk::BorderColor::eIntOpaqueBlack;
    sampler_info.unnormalizedCoordinates = false;
    sampler_info.compareEnable = false;
    sampler_info.compareOp = vk::CompareOp::eAlways;
    sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
    sampler_info.mipLodBias = 0.0f;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = 0.0f;

    auto sampler = state.device.createSampler(sampler_info);
    checkResult(sampler.result);

    return sampler.value;

}


vk::Buffer createVertexBuffer(RenderingState const& state, std::vector<Vertex> const& vertices)
{
    vk::DeviceSize buffer_size = sizeof(Vertex) * vertices.size();
    vk::DeviceMemory staging_buffer_memory;
    auto staging_buffer = createBuffer(state,
                               buffer_size, vk::BufferUsageFlagBits::eTransferSrc,
                               vk::MemoryPropertyFlagBits::eHostVisible
                             | vk::MemoryPropertyFlagBits::eHostCoherent, staging_buffer_memory);

    void* data;
    checkResult(state.device.mapMemory(staging_buffer_memory, 0, buffer_size, static_cast<vk::MemoryMapFlagBits>(0), &data));
    memcpy(data, vertices.data(), buffer_size);
    state.device.unmapMemory(staging_buffer_memory);

    vk::DeviceMemory vertex_buffer_memory;
    auto vertex_buffer = createBuffer(state, buffer_size,  vk::BufferUsageFlagBits::eTransferDst
                                                  | vk::BufferUsageFlagBits::eVertexBuffer,
                                      vk::MemoryPropertyFlagBits::eDeviceLocal, vertex_buffer_memory);

    copyBuffer(state, staging_buffer, vertex_buffer, buffer_size);

    state.device.destroyBuffer(staging_buffer);
    state.device.freeMemory(staging_buffer_memory);
    return vertex_buffer;
}

vk::Buffer createIndexBuffer(RenderingState const& state, std::vector<uint32_t> indices)
{
    vk::DeviceSize buffer_size = sizeof(decltype(indices)::value_type) * indices.size();

    vk::DeviceMemory staging_buffer_memory;
    auto staging_buffer = createBuffer(state,
                               buffer_size, vk::BufferUsageFlagBits::eTransferSrc,
                               vk::MemoryPropertyFlagBits::eHostVisible
                             | vk::MemoryPropertyFlagBits::eHostCoherent, staging_buffer_memory);

    void* data;
    checkResult(state.device.mapMemory(staging_buffer_memory, 0, buffer_size, static_cast<vk::MemoryMapFlagBits>(0), &data));
    memcpy(data, indices.data(), buffer_size);
    state.device.unmapMemory(staging_buffer_memory);

    vk::DeviceMemory indices_buffer_memory;
    auto index_buffer = createBuffer(state, buffer_size,  vk::BufferUsageFlagBits::eTransferDst
                                                  | vk::BufferUsageFlagBits::eIndexBuffer,
                                      vk::MemoryPropertyFlagBits::eDeviceLocal, indices_buffer_memory);

    copyBuffer(state, staging_buffer, index_buffer, buffer_size);

    state.device.destroyBuffer(staging_buffer);
    state.device.freeMemory(staging_buffer_memory);
    return index_buffer;
}

static vk::ShaderModule createShaderModule(std::vector<char> const& code, vk::Device const& device)
{
    std::cout << code.size() << '\n';

    vk::ShaderModuleCreateInfo create_info;
    create_info.sType = vk::StructureType::eShaderModuleCreateInfo;
    create_info.setCodeSize(code.size());
    create_info.setPCode(reinterpret_cast<const uint32_t*>(code.data()));

    auto shader_module = device.createShaderModule(create_info);

    return shader_module.value;


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
static constexpr std::array<vk::VertexInputAttributeDescription, 4> getAttributeDescriptions()
{
    std::array<vk::VertexInputAttributeDescription, 4> desc;

    desc[0].binding = 0;
    desc[0].location = 0;
    desc[0].format = vk::Format::eR32G32B32Sfloat;
    desc[0].offset = offsetof(Vertex, pos);

    desc[1].binding = 0;
    desc[1].location = 1;
    desc[1].format = vk::Format::eR32G32B32Sfloat;
    desc[1].offset = offsetof(Vertex, color);

    desc[2].binding = 0;
    desc[2].location = 2;
    desc[2].format = vk::Format::eR32G32Sfloat;
    desc[2].offset = offsetof(Vertex, tex_coord);

    desc[3].binding = 0;
    desc[3].location = 3;
    desc[3].format = vk::Format::eR32G32B32Sfloat;
    desc[3].offset = offsetof(Vertex, normal);
    return  desc;
}




std::pair<std::vector<vk::Pipeline>, vk::PipelineLayout>  createGraphicsPipline(vk::Device const& device, vk::Extent2D const& swap_chain_extent, vk::RenderPass const& render_pass, std::vector<vk::DescriptorSetLayout> const& desc_set_layout,
                                                                                std::vector<char> const& vertex_shader, std::vector<char> const& fragment_shader)
{
    std::string path;

    std::cout << "Create vertex\n";
    auto const vertex = createShaderModule(vertex_shader, device);
    std::cout << "Create frag\n";
    auto const frag = createShaderModule(fragment_shader, device);


    vk::PipelineShaderStageCreateInfo vert_shader_stage_info;
    vert_shader_stage_info.sType = vk::StructureType::ePipelineShaderStageCreateInfo;
    vert_shader_stage_info.stage = vk::ShaderStageFlagBits::eVertex;
    vert_shader_stage_info.module = vertex;
    vert_shader_stage_info.pName = "main";

    vk::PipelineShaderStageCreateInfo frag_shader_stage_info;
    frag_shader_stage_info.sType = vk::StructureType::ePipelineShaderStageCreateInfo;
    frag_shader_stage_info.stage = vk::ShaderStageFlagBits::eFragment;
    frag_shader_stage_info.module = frag;
    frag_shader_stage_info.pName = "main";

    vk::PipelineShaderStageCreateInfo shader_stages[] = {vert_shader_stage_info, frag_shader_stage_info};


    std::vector<vk::DynamicState> dynamic_states {
          vk::DynamicState::eViewport
        , vk::DynamicState::eScissor
    };

    vk::PipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = vk::StructureType::ePipelineDynamicStateCreateInfo;
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state.setDynamicStates(dynamic_states);


    vk::PipelineVertexInputStateCreateInfo vertex_input_info {};

    constexpr auto biding_descrition = getBindingDescription<Vertex>();
    constexpr auto attrib_descriptions = getAttributeDescriptions<Vertex>();

    vertex_input_info.sType = vk::StructureType::ePipelineVertexInputStateCreateInfo;
    vertex_input_info.setVertexBindingDescriptions(biding_descrition);
    vertex_input_info.setVertexAttributeDescriptions(attrib_descriptions);

    vk::PipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = vk::StructureType::ePipelineInputAssemblyStateCreateInfo;
    input_assembly.setTopology(vk::PrimitiveTopology::eTriangleList);
    input_assembly.setPrimitiveRestartEnable(false);

    vk::Viewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = (float)swap_chain_extent.width;
    viewport.height= (float)swap_chain_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vk::Rect2D scissor{};
    scissor.offset = vk::Offset2D(0,0);
    scissor.extent = swap_chain_extent;


    vk::PipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = vk::StructureType::ePipelineViewportStateCreateInfo;
    viewport_state.setViewportCount(1);
    viewport_state.setViewports(viewport);
    viewport_state.setScissorCount(1);
    viewport_state.setScissors(scissor);


    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = vk::StructureType::ePipelineRasterizationStateCreateInfo;
    rasterizer.depthClampEnable = false;
    rasterizer.rasterizerDiscardEnable = false;
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = vk::CullModeFlagBits::eBack;
    rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
    rasterizer.depthBiasEnable = false;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 0.0f;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = vk::StructureType::ePipelineMultisampleStateCreateInfo;
    multisampling.sampleShadingEnable = false;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = false;
    multisampling.alphaToOneEnable = false;

    vk::PipelineColorBlendAttachmentState color_blend_attachement{};
    color_blend_attachement.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                             vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

    
    color_blend_attachement.blendEnable = false;
    color_blend_attachement.srcColorBlendFactor = vk::BlendFactor::eOne;
    color_blend_attachement.dstColorBlendFactor = vk::BlendFactor::eZero;
    color_blend_attachement.colorBlendOp= vk::BlendOp::eAdd;
    color_blend_attachement.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    color_blend_attachement.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    color_blend_attachement.alphaBlendOp = vk::BlendOp::eAdd;

    vk::PipelineColorBlendStateCreateInfo color_blending;
    color_blending.sType = vk::StructureType::ePipelineColorBlendStateCreateInfo;
    color_blending.logicOpEnable = false;
    color_blending.logicOp = vk::LogicOp::eCopy;
    color_blending.attachmentCount = 1;
    color_blending.setAttachments(color_blend_attachement);
    color_blending.blendConstants[0] = 0.0f;
    color_blending.blendConstants[1] = 0.0f;
    color_blending.blendConstants[2] = 0.0f;
    color_blending.blendConstants[3] = 0.0f;

    vk::PipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = vk::StructureType::ePipelineLayoutCreateInfo;
    pipeline_layout_info.setSetLayouts(desc_set_layout);
    //
    vk::PushConstantRange range;
    range.setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);
    range.setSize(sizeof(ModelBufferObject));
    range.setOffset(0);

    //pipeline_layout_info.setPushConstantRanges(range);
    // pipeline_layout_info.pushConstantRangeCount = 0;
    // pipeline_layout_info.pPushConstantRanges = nullptr;


    auto pipeline_layout = device.createPipelineLayout(pipeline_layout_info);

    vk::PipelineDepthStencilStateCreateInfo depth_stencil;
    depth_stencil.sType = vk::StructureType::ePipelineDepthStencilStateCreateInfo;
    depth_stencil.depthTestEnable = true;
    depth_stencil.depthWriteEnable = true;

    depth_stencil.depthCompareOp = vk::CompareOp::eLess;
    depth_stencil.depthBoundsTestEnable = false;
    depth_stencil.minDepthBounds = 0.0f;
    depth_stencil.maxDepthBounds = 1.0f;
    depth_stencil.stencilTestEnable = false;


    vk::GraphicsPipelineCreateInfo pipeline_info;
    pipeline_info.sType = vk::StructureType::eGraphicsPipelineCreateInfo;
    pipeline_info.stageCount = 2;
    pipeline_info.setStages(shader_stages);


    pipeline_info.setPVertexInputState(&vertex_input_info);
    pipeline_info.setPInputAssemblyState(&input_assembly);
    pipeline_info.setPViewportState(&viewport_state);
    pipeline_info.setPRasterizationState(&rasterizer);
    pipeline_info.setPMultisampleState(&multisampling);
    pipeline_info.setPDepthStencilState(&depth_stencil);
    pipeline_info.setPColorBlendState(&color_blending);
    pipeline_info.setPDynamicState(&dynamic_state);

    pipeline_info.setLayout(pipeline_layout.value);
    pipeline_info.setRenderPass(render_pass);
    pipeline_info.setSubpass(0);
    pipeline_info.basePipelineIndex = 0;
    pipeline_info.basePipelineIndex = -1;

    auto pipelines = device.createGraphicsPipelines(VK_NULL_HANDLE, pipeline_info);

    return std::make_pair(pipelines.value, pipeline_layout.value);
}


void recreateSwapchain(RenderingState& state)
{
    std::cout << "Recreating swapchain" << '\n';
    auto result = state.device.waitIdle();

    // Destroy the swapchain
    std::for_each(state.framebuffers.begin(), state.framebuffers.end(), [&](auto const& f) { state.device.destroyFramebuffer(f);});
    std::for_each(state.image_views.begin(), state.image_views.end(), [&](auto const& f) { state.device.destroyImageView(f);});
    state.device.destroySwapchainKHR(state.swap_chain.swap_chain);

    // Create new swapchain
    state.swap_chain = createSwapchain(state.physical_device, state.device, state.surface, state.window, state.indices);
    state.image_views = createImageViews(state.swap_chain, state.device);
    state.depth_resources = createDepth(state);
    state.framebuffers = createFrameBuffers(state.device, state.image_views, state.render_pass,
                                           state.swap_chain.extent, state.depth_resources);

    std::cout << "Finish recreating\n";
}


