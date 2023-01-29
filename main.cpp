#define VK_USE_PLATFORM_XLIB_KHR
#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_X11
#include "GLFW/glfw3native.h"

#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_funcs.hpp>
#include <vulkan/vulkan_structs.hpp>

#include <iostream>
#include <optional>
#include <set>
#include <limits>
#include <fstream>

static const std::vector<const char*> device_extensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

struct SwapChainSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> present_modes;
};

SwapChainSupportDetails querySwapChainSupport(vk::PhysicalDevice const& device, vk::SurfaceKHR const& surface)
{
    SwapChainSupportDetails details;

    details.capabilities = device.getSurfaceCapabilitiesKHR(surface);
    details.formats = device.getSurfaceFormatsKHR(surface);
    details.present_modes = device.getSurfacePresentModesKHR(surface);

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
        if (available_present_mode == vk::PresentModeKHR::eMailbox)
        {
            return available_present_mode;
        }
    }

    return vk::PresentModeKHR::eFifo;
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


GLFWwindow* setupGlfw()
{
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    return glfwCreateWindow(800, 600, "Vulkan", nullptr, nullptr);
}

void loop(GLFWwindow* window)
{
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
    }
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

struct QueueFamilyIndices {
    std::optional<unsigned int> graphics_family;
    std::optional<unsigned int> present_family;
};

QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice const& physical_device, vk::SurfaceKHR const& surface)
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

        vk::Bool32 present_support = physical_device.getSurfaceSupportKHR(i, surface);

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

bool isDeviceSuitable(vk::PhysicalDevice const& device, vk::SurfaceKHR const& surface)
{
    QueueFamilyIndices indices = findQueueFamilies(device, surface);



    return true;
}

bool checkValidationLayerSupport(std::vector<const char*> const& validation_layers)
{
    auto layers = vk::enumerateInstanceLayerProperties();

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

vk::ShaderModule createShaderModule(std::vector<char> const& code, vk::Device const& device)
{
    vk::ShaderModuleCreateInfo create_info;
    create_info.sType = vk::StructureType::eShaderModuleCreateInfo;
    create_info.setCodeSize(code.size());
    create_info.setPCode(reinterpret_cast<const uint32_t*>(code.data()));

    auto shader_module = device.createShaderModule(create_info);

    return shader_module;


}

std::vector<char> readFile(std::string const& path)
{
    std::ifstream file(path);
    return std::vector<char>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

std::vector<vk::Pipeline> createGraphicsPipline(vk::Device const& device, vk::Extent2D const& swap_chain_extent, vk::RenderPass const& render_pass)
{
    std::string path;

    auto const vertex = createShaderModule(readFile("shaders/vert.spv"), device);
    auto const frag = createShaderModule(readFile("shaders/frag.spv"), device);


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
    vertex_input_info.sType = vk::StructureType::ePipelineVertexInputStateCreateInfo;
    vertex_input_info.vertexBindingDescriptionCount = 0;
    vertex_input_info.pVertexBindingDescriptions = nullptr;
    vertex_input_info.vertexAttributeDescriptionCount= 0;
    vertex_input_info.pVertexAttributeDescriptions = nullptr;
    

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
    rasterizer.frontFace = vk::FrontFace::eClockwise;
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
    pipeline_layout_info.setLayoutCount = 0;
    pipeline_layout_info.pSetLayouts = nullptr;
    pipeline_layout_info.pushConstantRangeCount = 0;
    pipeline_layout_info.pPushConstantRanges = nullptr;


    auto pipeline_layout = device.createPipelineLayout(pipeline_layout_info);

    vk::GraphicsPipelineCreateInfo pipeline_info;
    pipeline_info.sType = vk::StructureType::eGraphicsPipelineCreateInfo;
    pipeline_info.stageCount = 2;
    pipeline_info.setStages(shader_stages);

    pipeline_info.setPVertexInputState(&vertex_input_info);
    pipeline_info.setPInputAssemblyState(&input_assembly);
    pipeline_info.setPViewportState(&viewport_state);
    pipeline_info.setPRasterizationState(&rasterizer);
    pipeline_info.setPMultisampleState(&multisampling);
    pipeline_info.setPDepthStencilState(nullptr);
    pipeline_info.setPColorBlendState(&color_blending);
    pipeline_info.setPDynamicState(&dynamic_state);

    pipeline_info.setLayout(pipeline_layout);
    pipeline_info.setRenderPass(render_pass);
    pipeline_info.setSubpass(0);
    pipeline_info.basePipelineIndex = 0;
    pipeline_info.basePipelineIndex = -1;

    auto pipelines = device.createGraphicsPipelines(VK_NULL_HANDLE, pipeline_info);

    return pipelines.value;
}

vk::RenderPass createRenderPass(vk::Device const& device, vk::Format const& swap_chain_image_format)
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

    vk::AttachmentReference color_attachment_ref;
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::SubpassDescription subpass {};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.setColorAttachments(color_attachment_ref);

    vk::SubpassDependency dependency{};
    dependency.setSrcSubpass(VK_SUBPASS_EXTERNAL);
    dependency.setDstSubpass(0);

    dependency.setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
    dependency.setSrcAccessMask(static_cast<vk::AccessFlags>(0));

    dependency.setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
    dependency.setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);



    vk::RenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = vk::StructureType::eRenderPassCreateInfo;
    render_pass_info.attachmentCount = 1;
    render_pass_info.setAttachments(color_attachment);
    render_pass_info.subpassCount = 1;
    render_pass_info.setSubpasses(subpass);
    render_pass_info.setDependencies(dependency);

    return device.createRenderPass(render_pass_info);
}

std::vector<vk::Framebuffer> createFrameBuffers(vk::Device const& device, std::vector<vk::ImageView> const& swap_chain_image_views, vk::RenderPass const& render_pass, vk::Extent2D const& swap_chain_extent)
{
    std::vector<vk::Framebuffer> swap_chain_frame_buffers;

    for (auto const& swap_chain_image_view : swap_chain_image_views)
    {
        vk::ImageView attachments[] = {swap_chain_image_view};

        vk::FramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = vk::StructureType::eFramebufferCreateInfo;
        framebuffer_info.setRenderPass(render_pass);
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.setAttachments(attachments);
        framebuffer_info.width = swap_chain_extent.width;
        framebuffer_info.height = swap_chain_extent.height;
        framebuffer_info.layers = 1;

        swap_chain_frame_buffers.push_back(device.createFramebuffer(framebuffer_info));
    }

    return swap_chain_frame_buffers;
}

vk::CommandPool createCommandPool(vk::Device const& device, QueueFamilyIndices const& queue_family_indices)
{
    vk::CommandPoolCreateInfo pool_info;
    pool_info.sType = vk::StructureType::eCommandPoolCreateInfo;
    pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    pool_info.queueFamilyIndex = queue_family_indices.graphics_family.value();

    return device.createCommandPool(pool_info);

}

std::vector<vk::CommandBuffer> createCommandBuffer(vk::Device const& device, vk::CommandPool const& command_pool)
{
    vk::CommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = vk::StructureType::eCommandBufferAllocateInfo;
    alloc_info.commandPool = command_pool;
    alloc_info.level = vk::CommandBufferLevel::ePrimary;
    alloc_info.commandBufferCount = 1;

    auto command_buffers = device.allocateCommandBuffers(alloc_info);
    return command_buffers;
}

void recordCommandBuffer(vk::CommandBuffer command_buffer, uint32_t image_index, vk::RenderPass const& render_pass,
                         std::vector<vk::Framebuffer> const& swap_chain_framebuffers, vk::Extent2D const& swap_chain_extent,
                         vk::Pipeline const& graphics_pipeline)
{
    vk::CommandBufferBeginInfo begin_info{};
    begin_info.sType = vk::StructureType::eCommandBufferBeginInfo;
    begin_info.setFlags(static_cast<vk::CommandBufferUsageFlags>(0));
    begin_info.pInheritanceInfo = nullptr;

    command_buffer.begin(begin_info);

    vk::RenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = vk::StructureType::eRenderPassBeginInfo;
    render_pass_info.setRenderPass(render_pass);
    render_pass_info.setFramebuffer(swap_chain_framebuffers[image_index]);
    render_pass_info.renderArea.offset = vk::Offset2D{0,0};
    render_pass_info.renderArea.extent = swap_chain_extent;

    vk::ClearValue clear_color(vk::ClearColorValue(std::array<float,4>{0,0,0,1}));

    render_pass_info.clearValueCount = 1;
    render_pass_info.setClearValues(clear_color);

    command_buffer.beginRenderPass(&render_pass_info, vk::SubpassContents::eInline);
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphics_pipeline);

    vk::Viewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swap_chain_extent.width);
    viewport.height = static_cast<float>(swap_chain_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    command_buffer.setViewport(0,viewport);
    
    vk::Rect2D scissor{};
    scissor.offset = vk::Offset2D{0,0};
    scissor.extent = swap_chain_extent;
    command_buffer.setScissor(0,scissor);

    command_buffer.draw(3,1,0,0);

    command_buffer.endRenderPass();
    command_buffer.end();
}

void drawFrame(vk::Device const& device, vk::Fence const& fence, vk::SwapchainKHR const& swap_chain,
               vk::Semaphore const& image_available_semaphore, vk::Semaphore const& render_finished_semaphore,
               vk::CommandBuffer const& command_buffer,
               vk::RenderPass const& render_pass, std::vector<vk::Framebuffer> const& framebuffers,
               vk::Extent2D const& swap_chain_extent,  vk::Pipeline const& pipeline,
               vk::Queue const& graphics_queue, vk::Queue const& present_queue)
{
    auto v = device.waitForFences(fence, true, ~0);
    device.resetFences(fence);

    uint32_t image_index = device.acquireNextImageKHR(swap_chain, ~0, image_available_semaphore, VK_NULL_HANDLE).value;

    command_buffer.reset(static_cast<vk::CommandBufferResetFlags>(0));
    recordCommandBuffer(command_buffer, image_index, render_pass, framebuffers, swap_chain_extent, pipeline);

    vk::PipelineStageFlags wait_stages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};

    vk::SubmitInfo submit_info{};
    submit_info.sType = vk::StructureType::eSubmitInfo;
    submit_info.setWaitSemaphores(image_available_semaphore);
    submit_info.setWaitDstStageMask(wait_stages);
    submit_info.commandBufferCount = 1;
    submit_info.setCommandBuffers(command_buffer);
    submit_info.setSignalSemaphores(render_finished_semaphore);


    graphics_queue.submit(submit_info, fence);

    vk::PresentInfoKHR present_info{};
    present_info.sType = vk::StructureType::ePresentInfoKHR;
    present_info.setWaitSemaphores(render_finished_semaphore);
    present_info.setResults(nullptr);
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swap_chain;
    present_info.setImageIndices(image_index);

    auto result = present_queue.presentKHR(present_info);

}

auto createInstance(bool validation_layers_on)
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
    app_info.apiVersion = VK_API_VERSION_1_0;

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

    return  vk::createInstance(info, nullptr);
}

int main()
{
    auto window = setupGlfw();

    auto const instance = createInstance(true);

    vk::SurfaceKHR surface;
    glfwCreateWindowSurface(instance, window, nullptr, reinterpret_cast<VkSurfaceKHR_T**>(&surface));


    auto devices = instance.enumeratePhysicalDevices();
    if (devices.empty())
    {
        std::cout << "No GPU with vulkan support\n";
        return 0;
    }

    std::sort(devices.begin(), devices.end(), [](auto const& lhs, auto const& rhs) {return scoreDevice(lhs) < scoreDevice(rhs);});

    std::cout << "GPU count: " << devices.size() << '\n';
    vk::PhysicalDevice physical_device = devices.back();

    // Local devices
    auto indices = findQueueFamilies(physical_device, surface);

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

    auto device_features = physical_device.getFeatures();

    vk::DeviceCreateInfo device_info;
    device_info.sType = vk::StructureType::eDeviceCreateInfo;
    device_info.pQueueCreateInfos = &queue_create_info;
    device_info.queueCreateInfoCount = queue_create_infos.size();
    device_info.pEnabledFeatures = &device_features;

    device_info.enabledExtensionCount = device_extensions.size();
    device_info.ppEnabledExtensionNames = device_extensions.data();
    device_info.enabledLayerCount = 0;

    auto device = physical_device.createDevice(device_info, nullptr);

    auto graphics_queue = device.getQueue(*indices.graphics_family, 0);
    auto present_queue = device.getQueue(*indices.present_family, 0);

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
    swap_chain_create_info.setOldSwapchain(VK_NULL_HANDLE);

    auto swap_chain = device.createSwapchainKHR(swap_chain_create_info);

    auto swap_chain_images = device.getSwapchainImagesKHR(swap_chain);

    auto swap_chain_image_format = surface_format.format;
    auto swap_chain_extent = extent;



    std::vector<vk::ImageView> swap_chain_image_views;

    for (uint32_t i = 0; i < swap_chain_images.size(); ++i)
    {
        vk::ImageViewCreateInfo image_view_create_info {};
        image_view_create_info.sType = vk::StructureType::eImageViewCreateInfo;
        image_view_create_info.setImage(swap_chain_images[i]);

        image_view_create_info.setViewType(vk::ImageViewType::e2D);
        image_view_create_info.setFormat(swap_chain_image_format);

        image_view_create_info.components.r = vk::ComponentSwizzle::eIdentity;
        image_view_create_info.components.g = vk::ComponentSwizzle::eIdentity;
        image_view_create_info.components.b = vk::ComponentSwizzle::eIdentity;
        image_view_create_info.components.a = vk::ComponentSwizzle::eIdentity;

        image_view_create_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        image_view_create_info.subresourceRange.baseMipLevel = 0;
        image_view_create_info.subresourceRange.levelCount= 1;
        image_view_create_info.subresourceRange.baseArrayLayer = 0;
        image_view_create_info.subresourceRange.layerCount = 1;

        swap_chain_image_views.push_back(device.createImageView(image_view_create_info));
    }

    auto render_pass = createRenderPass(device, swap_chain_image_format);
    auto graphic_pipeline = createGraphicsPipline(device, swap_chain_extent, render_pass);
    auto swap_chain_framebuffers = createFrameBuffers(device, swap_chain_image_views, render_pass, swap_chain_extent);
    auto command_pool = createCommandPool(device, indices);
    auto command_buffers = createCommandBuffer(device, command_pool);

    vk::SemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = vk::StructureType::eSemaphoreCreateInfo;

    vk::FenceCreateInfo fence_info{};
    fence_info.sType = vk::StructureType::eFenceCreateInfo;
    fence_info.flags = vk::FenceCreateFlagBits::eSignaled;

    auto image_available_semaphore = device.createSemaphore(semaphore_info);
    auto render_finished_semaphore = device.createSemaphore(semaphore_info);
    auto in_fight_fence = device.createFence(fence_info);

    std::cout << "Graphic pipeline size: " << graphic_pipeline.size() << '\n';

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        drawFrame(device, in_fight_fence, swap_chain, image_available_semaphore, render_finished_semaphore,
                command_buffers.at(0), render_pass, swap_chain_framebuffers, swap_chain_extent, graphic_pipeline.at(0),
                graphics_queue, present_queue);
    }

    /*

    vk::XlibSurfaceCreateInfoKHR create_info;
    create_info.sType = vk::StructureType::eXlibSurfaceCreateInfoKHR;
    auto display = glfwGetX11Display();
    create_info.setDpy(display);
    create_info.setWindow(glfwGetX11Window(window));
    */

    //auto surface = instance.createXlibSurfaceKHR(create_info);

    // loop(window);
}
