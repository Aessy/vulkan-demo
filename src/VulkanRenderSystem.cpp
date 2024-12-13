#include "VulkanRenderSystem.h"
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_structs.hpp>

#define VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_RAII_NO_EXCEPTIONS
#define VULKAN_HPP_ASSERT_ON_RESULT
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"

#include <algorithm>
#include <set>

#include <spdlog/spdlog.h>

struct SwapChainSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormat2KHR> formats;
    std::vector<vk::PresentModeKHR> present_modes;
};

uint32_t findMemoryType(vk::PhysicalDevice const& physical_device, uint32_t type_filter, vk::MemoryPropertyFlags properties)
{
    auto mem_properties = physical_device.getMemoryProperties();

    for (uint32_t i = 0; i < mem_properties.memoryTypes.size(); ++i)
    {
        if (type_filter & (1 << i) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    spdlog::warn("Missing memory type");
    return 0;
}


static void framebufferResizeCallback(GLFWwindow* window, int, int)
{
    auto app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));

    app->window_resize = true;
}

static void keyPressedCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    auto app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
    app->events.push(Event{.key=key,.scancode=scancode,.action=action,.mods=mods});
}

GLFWwindow* setupGlfw(App& app)
{
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);


    auto window = glfwCreateWindow(1920, 1080, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(window, &app);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    glfwSetKeyCallback(window, keyPressedCallback);
    // glfwSetCursorPosCallback(window, cursorMovedCallback);


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


static auto createInstance(bool validation_layers_on, vk::raii::Context const& context)
{
    std::vector<const char*> const validation_layers
    {
        "VK_LAYER_KHRONOS_validation"
    };

    bool use_validation_layer = validation_layers_on && checkValidationLayerSupport(validation_layers);
    if (use_validation_layer)
    {
        spdlog::info("Using validation layers");
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

    std::vector<char const*> extensions;
    for (int i = 0; i< glfw_extensions_count; ++i)
    {
        extensions.push_back(glfw_extensions[i]);
    }

    extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
    extensions.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);



    info.enabledExtensionCount = extensions.size();
    info.ppEnabledExtensionNames = extensions.data();

    if (use_validation_layer)
    {
        info.setEnabledLayerCount(validation_layers.size());
        info.setPpEnabledLayerNames(validation_layers.data());
    }
    else
    {
        info.enabledLayerCount = 0;
    }

    spdlog::info("GLFW Extension count: {}", glfw_extensions_count);

    auto instance = vk::createInstance(info, nullptr);
    checkResult(instance.result);

    return vk::raii::Instance(context, instance.value);
}

static vk::raii::SurfaceKHR createSurface(vk::raii::Instance const& instance, GLFWwindow* window)
{
    vk::SurfaceKHR surface;
    glfwCreateWindowSurface(*instance, window, nullptr, reinterpret_cast<VkSurfaceKHR_T**>(&surface));

    return vk::raii::SurfaceKHR(instance, surface);
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

    spdlog::info("Device Score: {}", score);

    return score;
}

vk::SampleCountFlagBits getMaxUsableSampleCount(vk::PhysicalDevice device)
{
    auto properties = device.getProperties();

    auto counts = properties.limits.framebufferColorSampleCounts & properties.limits.framebufferDepthSampleCounts;
    if (counts & vk::SampleCountFlagBits::e64) { return vk::SampleCountFlagBits::e64;}
    if (counts & vk::SampleCountFlagBits::e32) { return vk::SampleCountFlagBits::e32;}
    if (counts & vk::SampleCountFlagBits::e16) { return vk::SampleCountFlagBits::e16;}
    if (counts & vk::SampleCountFlagBits::e8) { return vk::SampleCountFlagBits::e8;}
    if (counts & vk::SampleCountFlagBits::e4) { return vk::SampleCountFlagBits::e4;}
    if (counts & vk::SampleCountFlagBits::e2) { return vk::SampleCountFlagBits::e2;}

    return vk::SampleCountFlagBits::e1;
}

DepthResources createColorResources(RenderingState const& state, vk::Format format)
{
    auto [image, device_memory] = createImage(state, state.swap_chain.extent.width, state.swap_chain.extent.height, 1, 
        format, vk::ImageTiling::eOptimal,
                                vk::ImageUsageFlagBits::eColorAttachment
                                | vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlagBits::eDeviceLocal, state.msaa);

    transitionImageLayout(state, image, format, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal, 1);

    auto view = createImageView(state.device, image, format, vk::ImageAspectFlagBits::eColor, 1);

    return DepthResources{std::move(image), std::move(device_memory), std::move(view)};
}

static std::optional<vk::raii::PhysicalDevice> createPhysicalDevice(vk::raii::Instance const& instance)
{
    auto devices_exp = instance.enumeratePhysicalDevices();
    if (!devices_exp)
    {
        spdlog::error("No devices: {}");
    }

    auto devices = devices_exp.value();

    if (devices.empty())
    {
        spdlog::info("No GPU with vulkan support");
        return {};
    }

    std::sort(devices.begin(), devices.end(), [](auto const& lhs, auto const& rhs) {return scoreDevice(lhs) < scoreDevice(rhs);});

    spdlog::info("GPU count: {}", devices.size());
    return devices.back();
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
        }

        vk::Bool32 present_support = physical_device.getSurfaceSupportKHR(i, surface).value;

        if (present_support)
        {
            indices.present_family = i;
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

static vk::raii::Device createLogicalDevice(vk::raii::PhysicalDevice const& physical_device, QueueFamilyIndices const& indices)
{
    //
    // Local devices
    //


    auto result = physical_device.enumerateDeviceExtensionProperties();
    for (auto const& extension : result)
    {
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
    device_features.tessellationShader = true;
    
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
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
        VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME,
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

    return std::move(device.value());
}

static SwapChainSupportDetails querySwapChainSupport(vk::PhysicalDevice const& device, vk::SurfaceKHR const& surface)
{
    SwapChainSupportDetails details;

    details.capabilities = device.getSurfaceCapabilitiesKHR(surface).value;
    details.formats = device.getSurfaceFormats2KHR(surface).value;
    details.present_modes = device.getSurfacePresentModesKHR(surface).value;

    return details;
}

vk::SurfaceFormatKHR chooseSwapSurfaceFoprmat(std::vector<vk::SurfaceFormat2KHR> const& available_formats)
{
    for (auto const& available_format : available_formats)
    {
        // TODO: HDR support if available.
        spdlog::info("Available format: {} Color Space KHR: {}", vk::to_string(available_format.surfaceFormat.format),
                                                                 vk::to_string(available_format.surfaceFormat.colorSpace));
    }
    for (auto const& available_format : available_formats)
    {
        // TODO: Look more into HDR Surface format
        if (available_format.surfaceFormat.format == vk::Format::eB8G8R8A8Srgb && available_format.surfaceFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
        {
            return available_format.surfaceFormat;
        }
    }

    return available_formats[0].surfaceFormat;
}

vk::PresentModeKHR chooseSwapPresentMode(std::vector<vk::PresentModeKHR>const& present_modes)
{
    for (auto const& available_present_mode : present_modes)
    {
        spdlog::info("Present mode: {}", vk::to_string(available_present_mode));
        if (available_present_mode == vk::PresentModeKHR::eMailbox)
        {
            return available_present_mode;
        }
    }

    spdlog::info("Using fifo present mode");
    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities, GLFWwindow* window)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
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


static SwapChain createSwapchain(vk::raii::PhysicalDevice const& physical_device, vk::raii::Device const& device, vk::raii::SurfaceKHR const& surface, GLFWwindow* window, QueueFamilyIndices const& indices)
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

    vk::raii::SwapchainKHR swap_chain = device.createSwapchainKHR(swap_chain_create_info).value();
    auto swap_chain_images = vk::Device(device).getSwapchainImagesKHR(swap_chain);

    auto swap_chain_image_format = surface_format.format;
    auto swap_chain_extent = extent;

    SwapChain swap_chain_return {
        std::move(swap_chain),
        swap_chain_images.value,
        swap_chain_image_format,
        swap_chain_extent
    };

    return swap_chain_return;
}

static auto createImageViews(SwapChain const& sc, vk::raii::Device const& device)
{
    std::vector<vk::raii::ImageView> swap_chain_image_views;

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

        swap_chain_image_views.push_back(device.createImageView(image_view_create_info).value());
    }

    return swap_chain_image_views;
}

vk::Format getDepthFormat()
{
    vk::Format format = vk::Format::eD32Sfloat;
    return format;
}

static vk::RenderPass createRenderPass(vk::Device const& device, vk::Format const& swap_chain_image_format, vk::SampleCountFlagBits msaa)
{
    vk::AttachmentDescription2 color_attachment{};
    color_attachment.format = vk::Format::eR16G16B16A16Sfloat;
    color_attachment.samples = msaa;
    color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
    color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
    color_attachment.stencilLoadOp =  vk::AttachmentLoadOp::eDontCare;
    color_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    color_attachment.initialLayout = vk::ImageLayout::eUndefined;
    color_attachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::AttachmentDescription2 depth_attachment{};
    depth_attachment.format = getDepthFormat();
    depth_attachment.samples = msaa;
    depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
    depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
    depth_attachment.stencilLoadOp =  vk::AttachmentLoadOp::eDontCare;
    depth_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depth_attachment.initialLayout = vk::ImageLayout::eUndefined;
    depth_attachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::AttachmentDescription2 depth_attachment_resolve{};
    depth_attachment_resolve.format = getDepthFormat();
    depth_attachment_resolve.samples = vk::SampleCountFlagBits::e1;
    depth_attachment_resolve.loadOp = vk::AttachmentLoadOp::eDontCare;
    depth_attachment_resolve.storeOp = vk::AttachmentStoreOp::eStore;
    depth_attachment_resolve.stencilLoadOp =  vk::AttachmentLoadOp::eDontCare;
    depth_attachment_resolve.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depth_attachment_resolve.initialLayout = vk::ImageLayout::eUndefined;
    depth_attachment_resolve.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::AttachmentReference2 color_attachment_ref;
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::AttachmentReference2 depth_attachment_ref;
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::AttachmentReference2 depth_attachment_resolve_ref;
    depth_attachment_resolve_ref.attachment = 2;
    depth_attachment_resolve_ref.layout = vk::ImageLayout::eDepthAttachmentOptimal;

    vk::SubpassDescriptionDepthStencilResolve subpass_depth_resolve;
    subpass_depth_resolve.sType = vk::StructureType::eSubpassDescriptionDepthStencilResolve;
    subpass_depth_resolve.depthResolveMode = vk::ResolveModeFlagBits::eAverage;
    subpass_depth_resolve.stencilResolveMode = vk::ResolveModeFlagBits::eNone;
    subpass_depth_resolve.setPDepthStencilResolveAttachment(&depth_attachment_resolve_ref);

    vk::SubpassDescription2 subpass {};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.setColorAttachments(color_attachment_ref);
    subpass.setPDepthStencilAttachment(&depth_attachment_ref);
    subpass.pNext = &subpass_depth_resolve;


    vk::SubpassDependency2 dependency{};
    dependency.setSrcSubpass(VK_SUBPASS_EXTERNAL);
    dependency.setDstSubpass(0);

    dependency.setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests);
    dependency.setSrcAccessMask(static_cast<vk::AccessFlags>(0));

    dependency.setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests);
    dependency.setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite);

    std::array<vk::AttachmentDescription2, 3> attachments {color_attachment, depth_attachment, depth_attachment_resolve};

    vk::RenderPassCreateInfo2 render_pass_info{};
    render_pass_info.sType = vk::StructureType::eRenderPassCreateInfo2;
    render_pass_info.attachmentCount = attachments.size();
    render_pass_info.setAttachments(attachments);
    render_pass_info.subpassCount = 1;
    render_pass_info.setSubpasses(subpass);
    render_pass_info.setDependencies(dependency);

    return device.createRenderPass2(render_pass_info).value;
}

static vk::raii::CommandPool createCommandPool(vk::raii::Device const& device, QueueFamilyIndices const& queue_family_indices)
{
    vk::CommandPoolCreateInfo pool_info;
    pool_info.sType = vk::StructureType::eCommandPoolCreateInfo;
    pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    pool_info.queueFamilyIndex = queue_family_indices.graphics_family.value();

    return device.createCommandPool(pool_info).value();

}

static std::vector<vk::raii::CommandBuffer> createCommandBuffer(vk::raii::Device const& device, vk::raii::CommandPool const& command_pool)
{
    vk::CommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = vk::StructureType::eCommandBufferAllocateInfo;
    alloc_info.commandPool = command_pool;
    alloc_info.level = vk::CommandBufferLevel::ePrimary;
    alloc_info.commandBufferCount = 2;

    auto command_buffers = device.allocateCommandBuffers(alloc_info).value();

    return command_buffers;
}

static Semaphores createSemaphores(vk::raii::Device const& device)
{
    vk::SemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = vk::StructureType::eSemaphoreCreateInfo;

    vk::FenceCreateInfo fence_info{};
    fence_info.sType = vk::StructureType::eFenceCreateInfo;
    fence_info.flags = vk::FenceCreateFlagBits::eSignaled;

    Semaphores semaphores{};
    for (int i = 0; i < 2; ++i)
    {
        semaphores.in_flight_fence.push_back(std::make_unique<vk::raii::Fence>(std::move(device.createFence(fence_info).value())));
        semaphores.image_available_semaphore.push_back(std::make_unique<vk::raii::Semaphore>(std::move(device.createSemaphore(semaphore_info).value())));
        semaphores.render_finished_semaphore.push_back(std::make_unique<vk::raii::Semaphore>(std::move(device.createSemaphore(semaphore_info).value())));

    }

    return semaphores;
}

DepthResources createDepth(RenderingState const& state, vk::SampleCountFlagBits msaa, unsigned int layer_count)
{
    vk::Format format = getDepthFormat();

    auto [depth_image, device_memory] = createImage(state, state.swap_chain.extent.width, state.swap_chain.extent.height, 1, format,
                            vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst,
                            vk::MemoryPropertyFlagBits::eDeviceLocal, msaa, layer_count);

    

    transitionImageLayout(state, depth_image, format, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal, 1);

    auto depth_image_view = createImageView(state.device, depth_image, format, vk::ImageAspectFlagBits::eDepth, 1);

    return {std::move(depth_image), std::move(device_memory), std::move(depth_image_view)};
}


std::tuple<vk::raii::Image, vk::raii::DeviceMemory> createImage(RenderingState const& state, uint32_t width, uint32_t height, uint32_t mip_levels, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties, vk::SampleCountFlagBits n_samples, unsigned int layer_count)
{
    vk::ImageCreateInfo image_info;
    image_info.sType = vk::StructureType::eImageCreateInfo;
    image_info.imageType = vk::ImageType::e2D;
    image_info.extent.width = width;
    image_info.extent.height = height;
    image_info.extent.depth = 1;
    image_info.mipLevels = mip_levels;
    image_info.arrayLayers = layer_count;
    image_info.format = format;
    image_info.tiling = tiling;
    image_info.initialLayout = vk::ImageLayout::eUndefined;
    image_info.usage = usage;
    image_info.sharingMode = vk::SharingMode::eExclusive;
    image_info.samples = n_samples;
    image_info.flags = static_cast<vk::ImageCreateFlagBits>(0);

    vk::raii::Image texture_image = state.device.createImage(image_info).value();

    auto mem_reqs = texture_image.getMemoryRequirements();

    vk::MemoryAllocateInfo alloc_info{0};
    alloc_info.sType = vk::StructureType::eMemoryAllocateInfo;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = findMemoryType(state.physical_device, mem_reqs.memoryTypeBits, properties);

    vk::raii::DeviceMemory texture_image_memory = state.device.allocateMemory(alloc_info).value();
    texture_image.bindMemory(texture_image_memory, 0);

    return {std::move(texture_image), std::move(texture_image_memory)};
}

vk::raii::ImageView createImageView(vk::raii::Device const& device, vk::Image const& image, vk::Format format, vk::ImageAspectFlags aspec_flags, uint32_t mip_levels, vk::ImageViewType view_type, uint32_t level_count, uint32_t base_level)
{
    vk::ImageViewCreateInfo view_info;
    view_info.sType = vk::StructureType::eImageViewCreateInfo;
    view_info.image = image;
    view_info.viewType = view_type;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = aspec_flags;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.baseArrayLayer = base_level;
    view_info.subresourceRange.layerCount = level_count;
    view_info.subresourceRange.levelCount= mip_levels;

    auto texture_image_view = device.createImageView(view_info);

    return std::move(texture_image_view.value());
}

std::vector<std::unique_ptr<ImageResource>> createFogBuffer(RenderingState const& state, vk::MemoryPropertyFlags properties)
{
    vk::ImageCreateInfo create_info;
    create_info.sType = vk::StructureType::eImageCreateInfo;
    create_info.imageType = vk::ImageType::e3D;
    create_info.format = vk::Format::eR16Sfloat;
    create_info.extent.width = 128;
    create_info.extent.height = 128;
    create_info.extent.depth = 128;
    create_info.mipLevels = 1;
    create_info.arrayLayers = 1;
    create_info.samples = vk::SampleCountFlagBits::e1;
    create_info.tiling = vk::ImageTiling::eOptimal;
    create_info.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage;
    create_info.sharingMode = vk::SharingMode::eExclusive;
    create_info.initialLayout = vk::ImageLayout::eUndefined;

    std::vector<std::unique_ptr<ImageResource>> resources;
    for (int i = 0; i < 2; ++i)
    {
        vk::raii::Image fog_3d_texture = state.device.createImage(create_info).value();

        auto mem_reqs = fog_3d_texture.getMemoryRequirements();

        vk::MemoryAllocateInfo alloc_info{};
        alloc_info.sType = vk::StructureType::eMemoryAllocateInfo;
        alloc_info.allocationSize = mem_reqs.size;
        alloc_info.setMemoryTypeIndex(findMemoryType(state.physical_device, mem_reqs.memoryTypeBits, properties));

        auto buffer_memory = state.device.allocateMemory(alloc_info).value();

        fog_3d_texture.bindMemory(buffer_memory, 0);

        auto image_view = createImageView(state.device, fog_3d_texture, vk::Format::eR16Sfloat, vk::ImageAspectFlagBits::eColor, 1, vk::ImageViewType::e3D);

        transitionImageLayout(state, fog_3d_texture, vk::Format::eR16Sfloat,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, 1);

        resources.push_back(std::make_unique<ImageResource>(std::move(fog_3d_texture), std::move(buffer_memory), std::move(image_view)));
    }

    return resources;
}

void initImgui(vk::Device const& device, vk::PhysicalDevice const& physical, vk::Instance const& instance,
               vk::Queue const& queue, vk::RenderPass const& render_pass,
               RenderingState const& state, GLFWwindow* window, vk::SampleCountFlagBits msaa)
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
    init_info.MSAASamples = VkSampleCountFlagBits(msaa);

    ImGui_ImplVulkan_Init(&init_info, render_pass);

    auto cmd = beginSingleTimeCommands(state);

    ImGui_ImplVulkan_CreateFontsTexture(*cmd);
    endSingleTimeCommands(state, cmd);

    ImGui_ImplVulkan_DestroyFontUploadObjects();
}

std::optional<RenderingState> createVulkanRenderState()
{
    auto app = std::make_unique<App>();

    vk::raii::Context context;

    auto window = setupGlfw(*app);

    spdlog::info("Creating instance");
    auto instance = createInstance(true, context);

    spdlog::info("Creating surface");
    auto surface = createSurface(instance, window);

    spdlog::info("Creating physical device");
    auto physical_device = createPhysicalDevice(instance);
    if (!physical_device)
    {
        return {};
    }

    auto const msaa_samples = getMaxUsableSampleCount(*physical_device);
    auto const indices = findQueueFamilies(*physical_device, surface);

    auto device = createLogicalDevice(*physical_device, indices);

    auto sc = createSwapchain(*physical_device, device, surface, window, indices);

    auto swap_chain_image_views = createImageViews(sc, device);

    auto command_pool = createCommandPool(device, indices);
    auto command_buffers = createCommandBuffer(device, command_pool);

    auto semaphores = createSemaphores(device);

    auto graphics_queue = device.getQueue(*indices.graphics_family, 0).value();
    auto present_queue = device.getQueue(*indices.present_family, 0).value();

    auto const& properties = physical_device->getProperties();
    uint32_t uniform_buffer_alignment_min = properties.limits.minUniformBufferOffsetAlignment;

    spdlog::info("Uniform buffer alignment min size {}", uniform_buffer_alignment_min);

    RenderingState render_state {
        .context = std::move(context),
        .app = std::move(app),
        .window = window,
        .instance = std::move(instance),
        .surface = std::move(surface),
        .device = std::move(device),
        .indices = indices,
        .physical_device = std::move(*physical_device),
        .image_views = std::move(swap_chain_image_views),
        .swap_chain = std::move(sc),
        .semaphores = std::move(semaphores),
        .command_pool = std::move(command_pool),
        .command_buffer = std::move(command_buffers),
        .graphics_queue = std::move(graphics_queue),
        .present_queue = std::move(present_queue),
        .msaa = msaa_samples,
        .uniform_buffer_alignment_min = uniform_buffer_alignment_min
    };

    return render_state;
}

vk::raii::CommandBuffer beginSingleTimeCommands(RenderingState const& state)
{
    vk::CommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = vk::StructureType::eCommandBufferAllocateInfo;
    alloc_info.level = vk::CommandBufferLevel::ePrimary;
    alloc_info.commandPool = state.command_pool;
    alloc_info.commandBufferCount = 1;

    vk::raii::CommandBuffer cmd_buffer = std::move(state.device.allocateCommandBuffers(alloc_info).value()[0]);

    vk::CommandBufferBeginInfo begin_info{};
    begin_info.sType = vk::StructureType::eCommandBufferBeginInfo;
    begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    cmd_buffer.begin(begin_info);

    return cmd_buffer;
}

void endSingleTimeCommands(RenderingState const& state, vk::CommandBuffer const& cmd_buffer)
{
    checkResult(cmd_buffer.end());

    vk::SubmitInfo submit_info{};
    submit_info.sType = vk::StructureType::eSubmitInfo;
    submit_info.commandBufferCount = 1;
    submit_info.setCommandBuffers(cmd_buffer);

    state.graphics_queue.submit(submit_info);
    state.graphics_queue.waitIdle();
}

static void copyBuffer(RenderingState const& state, vk::raii::Buffer const& src_buffer, vk::raii::Buffer const& dst_buffer, vk::DeviceSize size)
{
    auto cmd_buffer = beginSingleTimeCommands(state);

    vk::BufferCopy copy_region{};
    copy_region.size = size;
    cmd_buffer.copyBuffer(src_buffer, dst_buffer, copy_region);

    endSingleTimeCommands(state, cmd_buffer);
}

bool hasStencilComponent(vk::Format format)
{
    return format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint;
}

void transitionImageLayout(vk::CommandBuffer const& cmd_buffer, vk::Image const& image, vk::Format const& format, vk::ImageLayout old_layout, vk::ImageLayout new_layout, uint32_t mip_levels, uint32_t layer_count)
{
    vk::ImageMemoryBarrier barrier{};
    barrier.sType = vk::StructureType::eImageMemoryBarrier;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = 0;
    barrier.dstQueueFamilyIndex = 0;
    barrier.image = image;

    if (new_layout == vk::ImageLayout::eDepthStencilAttachmentOptimal ||
        old_layout == vk::ImageLayout::eDepthStencilAttachmentOptimal)
    {
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;

        if (hasStencilComponent(format))
        {
            barrier.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
        }
    }
    else
    {
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    }
    
    
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mip_levels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layer_count;
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
    else if (old_layout == vk::ImageLayout::eUndefined && new_layout == vk::ImageLayout::eDepthStencilAttachmentOptimal)
    {
        barrier.srcAccessMask = vk::AccessFlags{};
        barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;

        src_stage = vk::PipelineStageFlagBits::eTopOfPipe;
        dest_stage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
    }
    else if (old_layout == vk::ImageLayout::eUndefined && new_layout == vk::ImageLayout::eColorAttachmentOptimal)
    {
        barrier.srcAccessMask = static_cast<vk::AccessFlagBits>(0);
        barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        src_stage = vk::PipelineStageFlagBits::eTopOfPipe;
        dest_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    }
    else if (old_layout == vk::ImageLayout::eColorAttachmentOptimal && new_layout == vk::ImageLayout::ePresentSrcKHR)
    {
        barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        barrier.dstAccessMask = vk::AccessFlags{};

        src_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dest_stage = vk::PipelineStageFlagBits::eBottomOfPipe;
    }
    else if (old_layout == vk::ImageLayout::eColorAttachmentOptimal && new_layout == vk::ImageLayout::eShaderReadOnlyOptimal)
    {
        barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        src_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dest_stage = vk::PipelineStageFlagBits::eFragmentShader;
    }
    else if (old_layout == vk::ImageLayout::eUndefined && new_layout == vk::ImageLayout::eGeneral)
    {
        barrier.srcAccessMask = static_cast<vk::AccessFlagBits>(0);
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        src_stage = vk::PipelineStageFlagBits::eTopOfPipe;
        dest_stage = vk::PipelineStageFlagBits::eTransfer;
    }
    else if (old_layout == vk::ImageLayout::eDepthStencilAttachmentOptimal && new_layout == vk::ImageLayout::eShaderReadOnlyOptimal)
    {
        barrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        src_stage = vk::PipelineStageFlagBits::eLateFragmentTests;
        dest_stage = vk::PipelineStageFlagBits::eFragmentShader;
    }
    else if (old_layout == vk::ImageLayout::eTransferDstOptimal && new_layout == vk::ImageLayout::eShaderReadOnlyOptimal)
    {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        src_stage = vk::PipelineStageFlagBits::eTransfer;
        dest_stage = vk::PipelineStageFlagBits::eFragmentShader;
    }
    else if (   old_layout == vk::ImageLayout::eShaderReadOnlyOptimal
             && new_layout == vk::ImageLayout::eDepthStencilAttachmentOptimal)
    {
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;

        src_stage = vk::PipelineStageFlagBits::eComputeShader;
        dest_stage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
    }
    else if (   old_layout == vk::ImageLayout::eDepthStencilAttachmentOptimal
             && new_layout == vk::ImageLayout::eTransferSrcOptimal)
    {
        barrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

        src_stage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
        dest_stage = vk::PipelineStageFlagBits::eTransfer;
    }
    else if (   old_layout == vk::ImageLayout::eDepthStencilAttachmentOptimal
             && new_layout == vk::ImageLayout::eTransferDstOptimal)
    {
        barrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        src_stage = vk::PipelineStageFlagBits::eLateFragmentTests;
        dest_stage = vk::PipelineStageFlagBits::eTransfer;
    }
    else if (   old_layout == vk::ImageLayout::eUndefined
             && new_layout == vk::ImageLayout::eTransferDstOptimal)
    {
        barrier.srcAccessMask = vk::AccessFlags{};
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        src_stage = vk::PipelineStageFlagBits::eTopOfPipe;
        dest_stage = vk::PipelineStageFlagBits::eTransfer;
    }
    else if (   old_layout == vk::ImageLayout::eTransferSrcOptimal
             && new_layout == vk::ImageLayout::eDepthStencilAttachmentOptimal)
    {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;

        src_stage = vk::PipelineStageFlagBits::eTransfer;
        dest_stage = vk::PipelineStageFlagBits::eLateFragmentTests;
    }
    else if (   old_layout == vk::ImageLayout::eGeneral
             && new_layout == vk::ImageLayout::eShaderReadOnlyOptimal)
    {
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        src_stage = vk::PipelineStageFlagBits::eComputeShader;
        dest_stage = vk::PipelineStageFlagBits::eFragmentShader;
    }
    else if (   old_layout == vk::ImageLayout::eShaderReadOnlyOptimal
             && new_layout == vk::ImageLayout::eGeneral)
    {
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.dstAccessMask = vk::AccessFlagBits{};

        src_stage = vk::PipelineStageFlagBits::eFragmentShader;
        dest_stage = vk::PipelineStageFlagBits::eBottomOfPipe;
    }
    else
    {
        spdlog::info("Error transition image");
        return;
    }

    cmd_buffer.pipelineBarrier(src_stage, dest_stage, vk::DependencyFlags{}, {}, {}, barrier);

}

void transitionImageLayout(RenderingState const& state, vk::Image const& image, vk::Format const& format, vk::ImageLayout old_layout, vk::ImageLayout new_layout, uint32_t mip_levels, uint32_t layer_count)
{
    auto cmd_buffer = beginSingleTimeCommands(state);

    transitionImageLayout(cmd_buffer, image, format, old_layout, new_layout, mip_levels, layer_count);

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

Buffer createBuffer(RenderingState const& state,
                    vk::DeviceSize size, vk::BufferUsageFlags usage,
                    vk::MemoryPropertyFlags properties)
{
    vk::BufferCreateInfo buffer_info{};
    buffer_info.sType = vk::StructureType::eBufferCreateInfo;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = vk::SharingMode::eExclusive;

    auto vertex_buffer = state.device.createBuffer(buffer_info);

    vk::raii::Buffer buffer = std::move(vertex_buffer.value());

    auto mem_reqs = buffer.getMemoryRequirements();

    vk::MemoryAllocateInfo alloc_info{};
    alloc_info.sType = vk::StructureType::eMemoryAllocateInfo;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.setMemoryTypeIndex(findMemoryType(state.physical_device, mem_reqs.memoryTypeBits, properties));

    auto buffer_memory = state.device.allocateMemory(alloc_info).value();
    buffer.bindMemory(buffer_memory, 0);

    return {std::move(buffer), std::move(buffer_memory)};
}

vk::raii::ImageView createTextureImageView(RenderingState const& state, vk::Image const& texture_image, vk::Format format, uint32_t mip_levels, uint32_t level_count)
{
    auto texture_image_view = createImageView(state.device, texture_image, format, vk::ImageAspectFlagBits::eColor, mip_levels, vk::ImageViewType::e2D, level_count, 0);
    return texture_image_view;
}

vk::raii::Sampler createDepthTextureSampler(RenderingState const& state)
{
    vk::SamplerCreateInfo sampler_info;
    sampler_info.sType = vk::StructureType::eSamplerCreateInfo;
    sampler_info.magFilter = vk::Filter::eLinear;
    sampler_info.minFilter = vk::Filter::eLinear;
    sampler_info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    sampler_info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    sampler_info.addressModeW = vk::SamplerAddressMode::eClampToEdge;    
    sampler_info.anisotropyEnable = true;
    sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
    sampler_info.mipLodBias = 0.0f;                           // No bias for LOD selection
    sampler_info.maxAnisotropy = 1.0f;
    sampler_info.minLod = 0.0f;                               // Only sample the base level (level 0)
    sampler_info.maxLod = 0.0f;                               // Only sample the base level (level 0)
    sampler_info.borderColor = vk::BorderColor::eIntOpaqueWhite; // Border color if sampler address mode is Border
    // sampler_info.compareEnable = true;
    // sampler_info.compareOp = vk::CompareOp::eLess;

    vk::raii::Sampler sampler = *state.device.createSampler(sampler_info);
    return sampler;
}

vk::raii::Sampler createTextureSampler(RenderingState const& state, bool mip_maps)
{
    vk::SamplerCreateInfo sampler_info;
    sampler_info.sType = vk::StructureType::eSamplerCreateInfo;
    sampler_info.magFilter = vk::Filter::eLinear;
    sampler_info.minFilter = vk::Filter::eLinear;
    sampler_info.addressModeU = vk::SamplerAddressMode::eRepeat;
    sampler_info.addressModeV = vk::SamplerAddressMode::eRepeat;
    sampler_info.addressModeW = vk::SamplerAddressMode::eRepeat;
    sampler_info.unnormalizedCoordinates = false;
    sampler_info.compareEnable = false;
    sampler_info.compareOp = vk::CompareOp::eAlways;
    
    if (!mip_maps)
    {
        sampler_info.mipLodBias = 0.0f;                           // No bias for LOD selection
        sampler_info.anisotropyEnable = VK_FALSE;                 // Disable anisotropic filtering for this sampler
        sampler_info.minLod = 0.0f;                               // Only sample the base level (level 0)
        sampler_info.maxLod = 0.0f;                               // Only sample the base level (level 0)
        sampler_info.borderColor = vk::BorderColor::eIntOpaqueBlack; // Border color if sampler address mode is Border
        sampler_info.mipmapMode = vk::SamplerMipmapMode::eNearest;
    }
    else
    {
        sampler_info.anisotropyEnable = true;
        auto properties = state.physical_device.getProperties();

        sampler_info.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
        sampler_info.borderColor = vk::BorderColor::eIntOpaqueBlack;
        sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
        sampler_info.mipLodBias = 0.0f;
        sampler_info.minLod = 0.0f;
        sampler_info.maxLod = 11.0f;
    }

    vk::raii::Sampler sampler = *state.device.createSampler(sampler_info);
    return sampler;
}


Buffer createVertexBuffer(RenderingState const& state, std::vector<Vertex> const& vertices)
{
    vk::DeviceSize buffer_size = sizeof(Vertex) * vertices.size();
    auto [staging_buffer, staging_buffer_memory] = createBuffer(state,
                               buffer_size, vk::BufferUsageFlagBits::eTransferSrc,
                               vk::MemoryPropertyFlagBits::eHostVisible
                             | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* data = staging_buffer_memory.mapMemory(0, buffer_size, static_cast<vk::MemoryMapFlagBits>(0));
    memcpy(data, vertices.data(), buffer_size);
    staging_buffer_memory.unmapMemory();

    auto [vertex_buffer, vertex_buffer_memory] = createBuffer(state, buffer_size,  vk::BufferUsageFlagBits::eTransferDst
                                                  | vk::BufferUsageFlagBits::eVertexBuffer,
                                      vk::MemoryPropertyFlagBits::eDeviceLocal);

    copyBuffer(state, staging_buffer, vertex_buffer, buffer_size);

    return {std::move(vertex_buffer), std::move(vertex_buffer_memory)};
}

Buffer createIndexBuffer(RenderingState const& state, std::vector<uint32_t> indices)
{
    vk::DeviceSize buffer_size = sizeof(decltype(indices)::value_type) * indices.size();

    auto [staging_buffer, staging_buffer_memory] = createBuffer(state,
                               buffer_size, vk::BufferUsageFlagBits::eTransferSrc,
                               vk::MemoryPropertyFlagBits::eHostVisible
                             | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* data = staging_buffer_memory.mapMemory(0, buffer_size, static_cast<vk::MemoryMapFlagBits>(0));
    memcpy(data, indices.data(), buffer_size);
    staging_buffer_memory.unmapMemory();

    auto [index_buffer, indices_buffer_memory] = createBuffer(state, buffer_size,  vk::BufferUsageFlagBits::eTransferDst
                                                  | vk::BufferUsageFlagBits::eIndexBuffer,
                                      vk::MemoryPropertyFlagBits::eDeviceLocal);

    copyBuffer(state, staging_buffer, index_buffer, buffer_size);
    return {std::move(index_buffer), std::move(indices_buffer_memory)};
}

vk::ShaderModule createShaderModule(std::vector<char> const& code, vk::Device const& device)
{
    spdlog::info("Creating shader module. Code size: {}", code.size());

    vk::ShaderModuleCreateInfo create_info;
    create_info.sType = vk::StructureType::eShaderModuleCreateInfo;
    create_info.setCodeSize(code.size());
    create_info.setPCode(reinterpret_cast<const uint32_t*>(code.data()));

    auto shader_module = device.createShaderModule(create_info);

    if (shader_module.result != vk::Result::eSuccess)
    {
        spdlog::warn("Failed creating shader module");
    }
    return shader_module.value;


}


std::pair<std::vector<vk::Pipeline>, vk::PipelineLayout> createComputePipeline(vk::Device const& device, ShaderStage const& compute_stage, std::vector<vk::DescriptorSetLayout> const& desc_set_layouts)
{
    vk::PipelineLayoutCreateInfo pipeline_layout_create_info;

    pipeline_layout_create_info.sType = vk::StructureType::ePipelineLayoutCreateInfo;
    pipeline_layout_create_info.setLayoutCount = 1;
    pipeline_layout_create_info.setSetLayouts(desc_set_layouts);
    auto result = device.createPipelineLayout(pipeline_layout_create_info);
    checkResult(result.result);
    auto pipeline_layout = result.value;

    vk::PipelineShaderStageCreateInfo stage_info;
    stage_info.sType = vk::StructureType::ePipelineShaderStageCreateInfo;
    stage_info.stage = compute_stage.stage;
    stage_info.module = compute_stage.module;
    stage_info.pName = "main";

    vk::ComputePipelineCreateInfo pipeline_create_info{};
    pipeline_create_info.sType = vk::StructureType::eComputePipelineCreateInfo;
    pipeline_create_info.stage = stage_info;
    pipeline_create_info.setLayout(pipeline_layout);

    auto pipeline_result = device.createComputePipelines(VK_NULL_HANDLE, pipeline_create_info);
    checkResult(pipeline_result.result);

    auto pipeline = pipeline_result.value;

    return std::make_pair(pipeline, pipeline_layout);
}


vk::PipelineRasterizationStateCreateInfo createRasterizerState()
{
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

    return rasterizer;
};


vk::PipelineDepthStencilStateCreateInfo createDepthStencil()
{
    vk::PipelineDepthStencilStateCreateInfo depth_stencil;
    depth_stencil.sType = vk::StructureType::ePipelineDepthStencilStateCreateInfo;
    depth_stencil.depthTestEnable = true;
    depth_stencil.depthWriteEnable = true;

    depth_stencil.depthCompareOp = vk::CompareOp::eLess;
    depth_stencil.depthBoundsTestEnable = false;
    depth_stencil.minDepthBounds = 0.0f;
    depth_stencil.maxDepthBounds = 1.0f;
    depth_stencil.stencilTestEnable = false;

    return depth_stencil;
}

GraphicsPipelineInput createDefaultPipelineInput()
{
    return {
        createRasterizerState(),
        createDepthStencil()
    };
}

std::pair<std::vector<vk::Pipeline>, vk::PipelineLayout>  createGraphicsPipline(vk::Device const& device,
                                                                                vk::Extent2D const& swap_chain_extent,
                                                                                vk::RenderPass const& render_pass,
                                                                                std::vector<vk::DescriptorSetLayout> const& desc_set_layout,
                                                                                std::vector<ShaderStage> shader_stages,
                                                                                vk::SampleCountFlagBits msaa,
                                                                                vk::PolygonMode polygon_mode,
                                                                                GraphicsPipelineInput const& input)
{
    std::string path;
    std::vector<vk::PipelineShaderStageCreateInfo> stages;

    vk::ShaderStageFlags shader_flags{};
    for (auto& stage : shader_stages)
    {
        vk::PipelineShaderStageCreateInfo stage_info;
        stage_info.sType = vk::StructureType::ePipelineShaderStageCreateInfo;
        stage_info.stage = stage.stage;
        stage_info.module = stage.module;
        stage_info.pName = "main";
        stages.push_back(stage_info);

        shader_flags |= stage_info.stage;
    }


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
    if (shader_flags & vk::ShaderStageFlagBits::eTessellationControl)
    {
        input_assembly.setTopology(vk::PrimitiveTopology::ePatchList);
    }
    else
    {
        input_assembly.setTopology(vk::PrimitiveTopology::eTriangleList);
    }

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


    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = vk::StructureType::ePipelineMultisampleStateCreateInfo;
    multisampling.sampleShadingEnable = false;
    multisampling.rasterizationSamples = msaa;
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
    range.setStageFlags(shader_flags);
    range.setSize(sizeof(ModelBufferObject));
    range.setOffset(0);

    auto pipeline_layout = device.createPipelineLayout(pipeline_layout_info);

    vk::GraphicsPipelineCreateInfo pipeline_info;
    pipeline_info.sType = vk::StructureType::eGraphicsPipelineCreateInfo;
    pipeline_info.stageCount = stages.size();
    pipeline_info.setStages(stages);


    pipeline_info.setPVertexInputState(&vertex_input_info);
    pipeline_info.setPInputAssemblyState(&input_assembly);
    pipeline_info.setPViewportState(&viewport_state);
    pipeline_info.setPRasterizationState(&input.rasterizer_state);
    pipeline_info.setPMultisampleState(&multisampling);
    pipeline_info.setPDepthStencilState(&input.depth_stencil);
    pipeline_info.setPColorBlendState(&color_blending);
    pipeline_info.setPDynamicState(&dynamic_state);

    pipeline_info.setLayout(pipeline_layout.value);
    pipeline_info.setRenderPass(render_pass);
    pipeline_info.setSubpass(0);
    pipeline_info.basePipelineIndex = 0;
    pipeline_info.basePipelineIndex = -1;

    vk::PipelineTessellationStateCreateInfo tess {};
    if (shader_flags | vk::ShaderStageFlagBits::eTessellationControl)
    {
        tess.sType = vk::StructureType::ePipelineTessellationStateCreateInfo;
        tess.pNext = nullptr;
        tess.patchControlPoints = 3;

        pipeline_info.setPTessellationState(&tess);
    }
    auto pipelines = device.createGraphicsPipelines(VK_NULL_HANDLE, pipeline_info);

    return std::make_pair(pipelines.value, pipeline_layout.value);
}
