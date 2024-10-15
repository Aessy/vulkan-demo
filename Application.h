#pragma once

#include "Textures.h"
#include "Mesh.h"
#include "Program.h"
#include "Scene.h"
#include "Model.h"
#include "VulkanRenderSystem.h"

#include <array>
#include <memory>
#include <vector>
#include <vulkan/vulkan_enums.hpp>

struct PostProcessing
{
    vk::RenderPass render_pass;
    std::vector<vk::Framebuffer> framebuffer;
};

struct Application
{
    Textures textures;
    Models models;
    Meshes meshes;
    std::vector<std::unique_ptr<Program>> programs;
    Scene scene;
    std::unique_ptr<Program> fog_program;
    std::vector<std::pair<vk::Image, vk::ImageView>> fog_buffer;

    PostProcessing post_processing_pass;
};

inline std::vector<vk::Framebuffer> createPostProcessingFramebuffers(RenderingState const& state,
                                                                     vk::RenderPass const& render_pass)
{
    std::vector<vk::Framebuffer> framebuffers;
    for (auto const& swap_chain_image_view : state.image_views)
    {
        std::array<vk::ImageView, 1> attachments {{swap_chain_image_view}};

        vk::FramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = vk::StructureType::eFramebufferCreateInfo;
        framebuffer_info.setRenderPass(render_pass);
        framebuffer_info.setAttachments(attachments);
        framebuffer_info.width = state.swap_chain.extent.width;
        framebuffer_info.height = state.swap_chain.extent.height;
        framebuffer_info.layers = 1;
        
        auto result = state.device.createFramebuffer(framebuffer_info);
        checkResult(result.result);
        framebuffers.push_back(result.value);
    }

    return framebuffers;
}

inline vk::RenderPass createPostProcessingRenderPass(vk::Format const swap_chain_image_format,
                                                     vk::Device const& device)
{
    vk::AttachmentDescription color_attachment{};
    color_attachment.format = swap_chain_image_format;
    color_attachment.samples = vk::SampleCountFlagBits::e1;
    color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
    color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
    color_attachment.stencilLoadOp =  vk::AttachmentLoadOp::eDontCare;
    color_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    color_attachment.initialLayout = vk::ImageLayout::eUndefined;
    color_attachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::AttachmentReference color_attachment_ref;
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::SubpassDescription subpass {};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.setColorAttachments(color_attachment_ref);

    vk::SubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;  // Previous render pass
    dependency.dstSubpass = 0;  // This post-processing subpass
    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
    dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead;

    vk::RenderPassCreateInfo renderPassInfo{};
    renderPassInfo.setAttachments(color_attachment);
    renderPassInfo.setSubpasses(subpass);
    renderPassInfo.setDependencies(dependency);

    // Create the render pass
    auto render_pass = device.createRenderPass(renderPassInfo);

    checkResult(render_pass.result);
    return render_pass.value;
}

inline PostProcessing createPostProcessing(RenderingState const& state)
{
    PostProcessing pp;
    pp.render_pass = createPostProcessingRenderPass(state.swap_chain.swap_chain_image_format, state.device);
    pp.framebuffer = createPostProcessingFramebuffers(state, pp.render_pass);

    return pp;
}