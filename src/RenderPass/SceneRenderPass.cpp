#include "SceneRenderPass.h"

#include "VulkanRenderSystem.h"
#include <vulkan/vulkan_enums.hpp>

#include "Application.h"
#include "Program.h"
#include "TypeLayer.h"
#include "Renderer.h"
#include "Scene.h"
#include "Pipelines/GeneralPurpuse.h"
#include "Pipelines/Skybox.h"

static void drawScene(vk::CommandBuffer& cmd_buffer, SceneRenderPass& scene_render_pass, Scene const& scene, int frame)
{
    // Write buffer data (storage and uniform)
    // The model buffer has to be bigger than all the buffers it contains.
    //
    // This is okay for now as the scene has few objects.
    // But it really should only be performed one time, and then change the buffers where
    // it is needed per frame like camera and buffer data. Also it does not have to be a part
    // of the command buffer pipeline like it kind of is now.
    for (auto const& o : scene.programs)
    {
        auto& program = scene_render_pass.pipelines[o.first];

        // Write buffer data (storage and uniform)
        // The model buffer has to be bigger than all the buffers it contains.
        for (auto& buffer : program.buffers)
        {
            if (auto* world = std::get_if<buffer_types::World>(&buffer))
            {
                auto ubo = createWorldBufferObject(scene);
                writeBuffer(world->buffers[frame], ubo);
            }
            else if (auto* model = std::get_if<buffer_types::Model>(&buffer))
            {
                for (size_t i = 0; i < o.second.size(); ++i)
                {
                    auto &obj = scene.objs[o.second[i]];
                    auto ubo = createModelBufferObject(obj);
                    writeBuffer(model->buffers[frame],ubo,i);
                }
            }
            else if (auto* material= std::get_if<buffer_types::MaterialShaderData>(&buffer))
            {
                for (size_t i = 0; i < o.second.size(); ++i)
                {
                    auto &obj = scene.objs[o.second[i]];
                    writeBuffer(material->buffers[frame],obj.material.shader_data, i);
                }
            }
            else if (auto* atmosphere = std::get_if<buffer_types::AtmosphereShaderData>(&buffer))
            {
                for (size_t i = 0; i < o.second.size(); ++i)
                {
                    writeBuffer(atmosphere->buffers[frame],scene.atmosphere);
                }
            }
        }

        cmd_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, program.pipeline);
        for (size_t i = 0; i < program.descriptor_sets.size(); ++i)
        {
            auto desc_type = program.descriptor_sets[i].layout_bindings[0].descriptorType;
            if (desc_type == vk::DescriptorType::eStorageBufferDynamic || desc_type == vk::DescriptorType::eUniformBufferDynamic)
            {
                uint32_t offset = 0;
                cmd_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, program.pipeline_layout, i, 1,&program.descriptor_sets[i].set[frame], 1, &offset);
            }
            else
            {
                cmd_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, program.pipeline_layout, i, 1,&program.descriptor_sets[i].set[frame], 0, nullptr);
            }
        }

        for (size_t i = 0; i < o.second.size(); ++i)
        {
            auto &drawable = scene.objs[o.second[i]];
            cmd_buffer.bindVertexBuffers(0, drawable.mesh.vertex_buffer, {0});
            cmd_buffer.bindIndexBuffer(drawable.mesh.index_buffer, 0, vk::IndexType::eUint32);
            
            cmd_buffer.drawIndexed(drawable.mesh.indices_size, 1, 0,0, i);
        }
    }
}

void sceneRenderPass(vk::CommandBuffer command_buffer,
                     RenderingState const& state,
                     SceneRenderPass& scene_render_pass,
                     Scene const& scene_data,
                     uint32_t image_index)
{
    vk::RenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = vk::StructureType::eRenderPassBeginInfo;
    render_pass_info.setRenderPass(scene_render_pass.render_pass);
    render_pass_info.setFramebuffer(scene_render_pass.framebuffers[image_index].framebuffer);
    render_pass_info.renderArea.offset = vk::Offset2D{0,0};
    render_pass_info.renderArea.extent = state.swap_chain.extent;

    std::array<vk::ClearValue, 2> clear_values{};
    clear_values[0].color = vk::ClearColorValue(std::array<float,4>{0.3984,0.695,1});
    clear_values[1].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);

    render_pass_info.clearValueCount = clear_values.size();
    render_pass_info.setClearValues(clear_values);


    vk::Viewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(state.swap_chain.extent.width);
    viewport.height = static_cast<float>(state.swap_chain.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    command_buffer.setViewport(0,viewport);

    vk::Rect2D scissor{};
    scissor.offset = vk::Offset2D{0,0};
    scissor.extent = state.swap_chain.extent;
    command_buffer.setScissor(0,scissor);

    command_buffer.beginRenderPass(&render_pass_info,
                                   vk::SubpassContents::eInline);
    drawScene(command_buffer, scene_render_pass, scene_data, state.current_frame);

    command_buffer.endRenderPass();

}

static std::vector<SceneFramebufferState> createFrameBuffers(RenderingState const& state, vk::RenderPass render_pass)
{
    std::vector<SceneFramebufferState> swap_chain_frame_buffers;
    for (auto const& swap_chain_image_view : state.image_views)
    {
        auto color_resources = createColorResources(state, vk::Format::eR16G16B16A16Sfloat);
        auto depth_image = createDepth(state, state.msaa);
        auto depth_resolve_image = createDepth(state, vk::SampleCountFlagBits::e1);

        std::array<vk::ImageView, 3> attachments = {color_resources.depth_image_view, depth_image.depth_image_view,
                                                        depth_resolve_image.depth_image_view};

        vk::FramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = vk::StructureType::eFramebufferCreateInfo;
        framebuffer_info.setRenderPass(render_pass);
        framebuffer_info.attachmentCount = attachments.size();
        framebuffer_info.setAttachments(attachments);
        framebuffer_info.width = state.swap_chain.extent.width;
        framebuffer_info.height = state.swap_chain.extent.height;
        framebuffer_info.layers = 1;

        auto framebuffer = state.device.createFramebuffer(framebuffer_info);
        checkResult(framebuffer.result);

        swap_chain_frame_buffers.push_back({SceneFramebufferState{.framebuffer = framebuffer.value,
                                                                    .color_resource = color_resources,
                                                                    .depth_resource = depth_image,
                                                                    .depth_resolve_resource = depth_resolve_image}});
    }

    return swap_chain_frame_buffers;
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

SceneRenderPass createSceneRenderPass(RenderingState const& state, Textures const& textures)
{
    auto render_pass = createRenderPass(state.device, state.swap_chain.swap_chain_image_format, state.msaa);
    auto framebuffers = createFrameBuffers(state, render_pass);

    auto scene_render_pass = SceneRenderPass
    {
        .render_pass = render_pass,
        .framebuffers = framebuffers,
    };

    scene_render_pass.pipelines.push_back(createGeneralPurposePipeline(state, render_pass, textures));
    scene_render_pass.pipelines.push_back(createSkyboxPipeline(state, render_pass));

    return scene_render_pass;
}