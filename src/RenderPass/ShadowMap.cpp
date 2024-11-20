#include "ShadowMap.h"
#include "Model.h"

#include <glm/glm.hpp>

#include "TypeLayer.h"
#include "VulkanRenderSystem.h"
#include "Program.h"
#include "Scene.h"
#include "Renderer.h"

#include <limits>
#include <vector>

#include <spdlog/spdlog.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

std::vector<glm::vec4> getFrustumCorners(glm::mat4 const& proj_view)
{
    auto const inv = glm::inverse(proj_view);

    std::vector<glm::vec4> frustum_corners;

    for (unsigned int x = 0; x < 2; ++x)
    {
        for (unsigned int y = 0; y < 2; ++y)
        {

            for (unsigned int z = 0; z < 2; ++z)
            {
                glm::vec4 pt = inv * glm::vec4(2.0f * x - 1.0,
                                               2.0f * y - 1.0f,
                                               2.0f * z - 1.0f,
                                               1.0f);
                frustum_corners.push_back(pt / pt.w);
            }
        }
    }

    return frustum_corners;
}

std::vector<glm::vec4> getFrustumCornersWorldSpace(glm::mat4 const& proj, glm::mat4 const& view)
{
    return getFrustumCorners(proj * view);
}

glm::vec3 getCenter(std::vector<glm::vec4> const& corners)
{
    glm::vec3 center = glm::vec3(0,0,0);

    for (auto const& v : corners)
    {
        center += glm::vec3(v);
    }

    center /= corners.size();
    return center;
}

static glm::mat4 getLightProjection(glm::mat4 const& light_view, std::vector<glm::vec4> const& frustum_corners)
{
    float min_x = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float min_y = std::numeric_limits<float>::max();
    float max_y = std::numeric_limits<float>::lowest();
    float min_z = std::numeric_limits<float>::max();
    float max_z = std::numeric_limits<float>::lowest();

    for (auto const& v : frustum_corners)
    {
        const auto trf = light_view * v;
        min_x = std::min(min_x, trf.x);
        max_x = std::max(max_x, trf.x);
        min_y = std::min(min_y, trf.y);
        max_y = std::max(max_y, trf.y);
        min_z = std::min(min_z, trf.z);
        max_z = std::max(max_z, trf.z);
    }

/*
    constexpr float z_mult = 10.0f;
    if (min_z < 0)
    {
        min_z *= z_mult;
    }
    else
    {
        min_z /= z_mult;
    }
    if (max_z < 0)
    {
        max_z /= z_mult;
    }
    else
    {
        max_z *= z_mult;
    }
*/

    glm::mat4 const light_projection = glm::ortho(min_x, max_x, max_y, min_y, min_z, max_z);

    return light_projection * light_view;
}

static glm::mat4 getLightSpaceMatrix(float const near, float const far, vk::Extent2D framebuffer_size, Camera const& camera, glm::vec3 const& light_dir)
{
    // Perspective from the camera with clipped near and far
    auto const projection = glm::perspective(glm::radians(45.0f),
                                             float(framebuffer_size.width) / float(framebuffer_size.height),
                                             near, far);

    auto const view = glm::lookAt(camera.pos, (camera.pos + camera.camera_front), camera.up);
    auto const corners = getFrustumCornersWorldSpace(projection, view);
    auto const center = getCenter(corners);

    auto const light_view = glm::lookAt(center + light_dir, center, glm::vec3(0.0f, 1.0f, 0.0f));

    auto const light_projection = getLightProjection(light_view, corners);
    return light_projection;
}

std::vector<glm::mat4> getLightSpaceMatrices(vk::Extent2D const& size, Camera const& camera, glm::vec3 const& light_dir)
{
    float const near = 0.5f;
    float const far = 1000.0f;

    float const level_1 = far/50; // 20
    float const level_2 = far/25; // 40
    float const level_3 = far/10; // 100
    float const level_4 = far/5;  // 500 

    std::vector<glm::mat4> matrices;
    matrices.push_back(getLightSpaceMatrix(near,         level_1, size, camera, light_dir));   // 0.5 - 20
    matrices.push_back(getLightSpaceMatrix(level_1, level_2, size, camera, light_dir));  // 20 - 40
    matrices.push_back(getLightSpaceMatrix(level_2, level_3, size, camera, light_dir));  // 40 - 100
    matrices.push_back(getLightSpaceMatrix(level_3, level_4, size, camera, light_dir));  // 100 - 500
    matrices.push_back(getLightSpaceMatrix(level_4,      far, size, camera, light_dir));     //  500 - 1000

    return matrices;
}

void drawShadowMap(vk::CommandBuffer command_buffer,
                   unsigned int cascade,
                   Scene const& scene,
                   CascadedShadowMap& shadow_map,
                   unsigned int frame,
                   glm::mat4 const& light_space_matrix)
{
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                shadow_map.pipeline.pipeline);


    uint32_t offset = cascade * sizeof(glm::mat4);
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            shadow_map.pipeline.pipeline_layout,
                            0,
                            1,
                            &shadow_map.pipeline.descriptor_sets[0].set[frame],
                            1,
                            &offset);

    offset = 0;
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            shadow_map.pipeline.pipeline_layout,
                            1,
                            1,
                            &shadow_map.pipeline.descriptor_sets[1].set[frame],
                            1,
                            &offset);

                            

    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            shadow_map.pipeline.pipeline_layout,
                            2,
                            1,
                            &shadow_map.pipeline.descriptor_sets[2].set[frame],
                            0,
                            nullptr);


    for (size_t i = 0; i < scene.objs.size(); ++i)
    {
        auto &drawable = scene.objs[i];
        if (drawable.shadow)
        {
            command_buffer.bindVertexBuffers(0, drawable.mesh.vertex_buffer, {0});
            command_buffer.bindIndexBuffer(drawable.mesh.index_buffer, 0, vk::IndexType::eUint32);
            command_buffer.drawIndexed(drawable.mesh.indices_size, 1, 0,0, i);
        }
    }

}

void shadowMapRenderPass(RenderingState const& state, CascadedShadowMap& shadow_map, Scene const& scene, vk::CommandBuffer command_buffer)
{
    glm::vec3 light_dir = glm::normalize(scene.light.sun_pos);
    auto const light_space_matrix = getLightSpaceMatrices(state.swap_chain.extent, scene.camera, light_dir);

    for (auto& buffer : shadow_map.pipeline.buffers)
    {
        // Write all the objects that should be included in the shadow map
        if (auto* model = std::get_if<buffer_types::Model>(&buffer))
        {
            for (size_t i = 0; i < scene.objs.size(); ++i)
            {
                auto &obj = scene.objs[i];
                if (obj.shadow)
                {
                    auto ubo = createModelBufferObject(obj);
                    writeBuffer(model->buffers[state.current_frame],ubo,i);
                }
            }
        }
        else if (auto* model = std::get_if<buffer_types::CascadedShadowMapBufferObject>(&buffer))
        {
            for (int i = 0; i < light_space_matrix.size(); ++i)
            {
                writeBuffer(model->buffers[state.current_frame], light_space_matrix[i], i);
            }
        }
        else if (auto* world = std::get_if<buffer_types::World>(&buffer))
        {
            auto ubo = createWorldBufferObject(scene);
            writeBuffer(world->buffers[state.current_frame], ubo);
        }
    }

    for (int i = 0; i < shadow_map.framebuffer_data.framebuffers[state.current_frame].size(); ++i)
    {
        auto current_framebuffer = shadow_map.framebuffer_data.framebuffers[state.current_frame][i];

        std::array<vk::ClearValue, 2> clear_values{};
        clear_values[0].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);

        vk::Viewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(state.swap_chain.extent.width);
        viewport.height = static_cast<float>(state.swap_chain.extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor{};
        scissor.offset = vk::Offset2D{0,0};
        scissor.extent = state.swap_chain.extent;

        command_buffer.setScissor(0,scissor);
        command_buffer.setViewport(0,viewport);

        vk::RenderPassBeginInfo render_pass_info{};
        render_pass_info.sType = vk::StructureType::eRenderPassBeginInfo;
        render_pass_info.setRenderPass(shadow_map.render_pass);
        render_pass_info.setFramebuffer(current_framebuffer);
        render_pass_info.setClearValues(clear_values);
        render_pass_info.renderArea.offset = vk::Offset2D(0,0);
        render_pass_info.renderArea.extent = state.swap_chain.extent;

        command_buffer.beginRenderPass(render_pass_info,
                                    vk::SubpassContents::eInline);

        drawShadowMap(command_buffer, i, scene, shadow_map, state.current_frame, glm::mat4());

        command_buffer.endRenderPass();
    }
}

static ShadowMapFramebuffer createCascadedShadowmapFramebuffers(RenderingState const& state,
                                                                vk::RenderPass const& render_pass,
                                                                unsigned int num_cascades)
{
    ShadowMapFramebuffer data;

    for (int i = 0; i < 2; ++i)
    {
        auto const depth_image = createDepth(state, vk::SampleCountFlagBits::e1, num_cascades);
        // Store the image and image view to the array for use in other render pass steps.
        data.cascade_images[i] = depth_image.depth_image;
        data.image_views[i] = createImageView(state.device, depth_image.depth_image, getDepthFormat(),
                                                  vk::ImageAspectFlagBits::eDepth, 1, vk::ImageViewType::e2DArray,
                                                  num_cascades, 0);
                                            

        for (int cascade = 0; cascade < num_cascades; ++cascade)
        {

            auto image_view = createImageView(state.device, depth_image.depth_image, getDepthFormat(),
                                                vk::ImageAspectFlagBits::eDepth, 1, vk::ImageViewType::e2DArray,
                                                1, cascade);

            std::array<vk::ImageView, 1> attachments = {image_view};
            vk::FramebufferCreateInfo framebuffer_info{};
            framebuffer_info.sType = vk::StructureType::eFramebufferCreateInfo;
            framebuffer_info.setRenderPass(render_pass);
            framebuffer_info.attachmentCount = attachments.size();
            framebuffer_info.setAttachments(attachments);
            framebuffer_info.width = state.swap_chain.extent.width;
            framebuffer_info.height = state.swap_chain.extent.height;
            framebuffer_info.layers = 1;

            auto result = state.device.createFramebuffer(framebuffer_info);
            checkResult(result.result);

            data.framebuffers[i].push_back(result.value);
        }
    }

    return data;
}

static std::tuple<vk::Pipeline, vk::PipelineLayout> createCascadedShadowMapPipeline(PipelineData const& pipeline_data,
                                                        vk::Extent2D const& swap_chain_extent,
                                                        vk::Device const& device,
                                                        vk::RenderPass const& render_pass)
{

    vk::ShaderStageFlags shader_flags{};

    std::vector<vk::PipelineShaderStageCreateInfo> stages;
    for (auto& stage : pipeline_data.shader_stages)
    {
        vk::PipelineShaderStageCreateInfo stage_info;
        stage_info.sType = vk::StructureType::ePipelineShaderStageCreateInfo;
        stage_info.stage = stage.stage;
        stage_info.module = stage.module;
        stage_info.pName = "main";
        stages.push_back(stage_info);

        shader_flags |= stage_info.stage;
    }

    // Expect Vertex and Fragment shader
    if (   (shader_flags & vk::ShaderStageFlagBits::eVertex
         &&(shader_flags & vk::ShaderStageFlagBits::eFragment)) == 0)
    {
        spdlog::info("Invalid shader modules");
        return {};
    }

    vk::PipelineVertexInputStateCreateInfo vertex_input_info {};

    constexpr auto biding_descrition = getBindingDescription<Vertex>();
    constexpr auto attrib_descriptions = getAttributeDescriptions<Vertex>();

    vertex_input_info.sType = vk::StructureType::ePipelineVertexInputStateCreateInfo;
    vertex_input_info.setVertexBindingDescriptions(biding_descrition);
    vertex_input_info.setVertexAttributeDescriptions(attrib_descriptions);

    vk::PipelineRasterizationStateCreateInfo rasterization_state{};
    rasterization_state.depthClampEnable = VK_FALSE;
    rasterization_state.rasterizerDiscardEnable = VK_FALSE;
    rasterization_state.polygonMode = vk::PolygonMode::eFill;
    rasterization_state.cullMode = vk::CullModeFlagBits::eBack;  // Cull back faces
    rasterization_state.frontFace = vk::FrontFace::eClockwise;
    rasterization_state.lineWidth = 1.0f;

    vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{};
    depth_stencil_state.depthTestEnable = true;
    depth_stencil_state.depthWriteEnable = true;
    depth_stencil_state.depthCompareOp = vk::CompareOp::eLess;
    depth_stencil_state.depthBoundsTestEnable = false;
    depth_stencil_state.stencilTestEnable = false;
    depth_stencil_state.minDepthBounds = 0.0f;
    depth_stencil_state.maxDepthBounds = 1.0f;

    vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{};
    input_assembly_state.topology = vk::PrimitiveTopology::eTriangleList;
    input_assembly_state.primitiveRestartEnable = VK_FALSE;

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
    viewport_state.setViewports(viewport);
    viewport_state.setScissors(scissor);

    vk::PipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = vk::StructureType::ePipelineLayoutCreateInfo;
    pipeline_layout_info.setSetLayouts(pipeline_data.descriptor_set_layouts);

    auto pipeline_layout = device.createPipelineLayout(pipeline_layout_info);
    checkResult(pipeline_layout.result);

    vk::GraphicsPipelineCreateInfo pipeline_info;
    pipeline_info.sType = vk::StructureType::eGraphicsPipelineCreateInfo;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = vk::StructureType::ePipelineMultisampleStateCreateInfo;
    multisampling.sampleShadingEnable = false;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = false;
    multisampling.alphaToOneEnable = false;

    pipeline_info.setPVertexInputState(&vertex_input_info);
    pipeline_info.setStages(stages);
    pipeline_info.setPInputAssemblyState(&input_assembly_state);
    pipeline_info.setPViewportState(&viewport_state);
    pipeline_info.setPRasterizationState(&rasterization_state);
    pipeline_info.setPDepthStencilState(&depth_stencil_state);
    pipeline_info.setPMultisampleState(&multisampling);
    pipeline_info.setLayout(pipeline_layout.value);
    pipeline_info.setRenderPass(render_pass);
    pipeline_info.setSubpass(0);
    pipeline_info.basePipelineIndex = 0;
    pipeline_info.basePipelineIndex = -1;

    auto pipeline = device.createGraphicsPipelines(VK_NULL_HANDLE, pipeline_info);
    checkResult(pipeline.result);

    return {pipeline.value[0], pipeline_layout.value};
}


static vk::RenderPass createShadowMapRenderPass(vk::Device const& device)
{

    vk::AttachmentDescription2 depth_attachment{};
    depth_attachment.format = vk::Format::eD32Sfloat;
    depth_attachment.samples = vk::SampleCountFlagBits::e1;
    depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
    depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
    depth_attachment.stencilLoadOp =  vk::AttachmentLoadOp::eDontCare;
    depth_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depth_attachment.initialLayout = vk::ImageLayout::eUndefined;
    depth_attachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::AttachmentReference2 depth_attachment_ref{};
    depth_attachment_ref.attachment = 0;
    depth_attachment_ref.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::SubpassDescription2 subpass {};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;

    // TODO: Probably need fixes here
    vk::SubpassDependency2 dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;  // Previous render pass
    dependency.dstSubpass = 0;  // This post-processing subpass
    dependency.setSrcStageMask(vk::PipelineStageFlagBits::eEarlyFragmentTests);
    dependency.setSrcAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentRead);

    dependency.dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
    dependency.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    std::array<vk::AttachmentDescription2, 1> attachments {depth_attachment};
    vk::RenderPassCreateInfo2 renderPassInfo{};
    renderPassInfo.setAttachments(attachments);
    renderPassInfo.setAttachmentCount(1);
    renderPassInfo.setSubpasses(subpass);
    renderPassInfo.setDependencies(dependency);

    // Create the render pass
    auto render_pass = device.createRenderPass2(renderPassInfo);

    checkResult(render_pass.result);
    return render_pass.value;
}

CascadedShadowMap createCascadedShadowMap(RenderingState const& core, Scene const& scene)
{
    CascadedShadowMap shadow_map;
    constexpr unsigned int n_cascades = shadow_map.n_cascaded_shadow_maps;
    shadow_map.render_pass = createShadowMapRenderPass(core.device);
    shadow_map.framebuffer_data = createCascadedShadowmapFramebuffers(core, shadow_map.render_pass, n_cascades);

    shadow_map.cascaded_shadow_map_buffer = createUniformBuffers<CascadedShadowMapBufferObject>(core, shadow_map.n_cascaded_shadow_maps);

    // Create pipeline
    layer_types::Program program_desc;
    program_desc.fragment_shader = {{"./shaders/cascaded_shadow_frag.spv"}};
    program_desc.vertex_shader= {{"./shaders/cascaded_shadow_vert.spv"}};

    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"shadow map buffer object"}},
        .type = layer_types::BufferType::CascadedShadowMapBufferObject,
        .count = 5,
        .size = 1,
        .buffer = shadow_map.cascaded_shadow_map_buffer,
        .binding = layer_types::Binding {
            .name = {{"binding matrice"}},
            .binding = 0,
            .type = layer_types::BindingType::Uniform,
            .size = 1,
            .vertex = true,
        }
    }});

    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"world_buffer"}},
        .type = layer_types::BufferType::WorldBufferObject,
        .size = 1,
        .buffer = scene.world_buffer,
        .binding = layer_types::Binding {
            .name = {{"binding world"}},
            .binding = 0,
            .type = layer_types::BindingType::Uniform,
            .size = 1,
            .vertex = true,
        }
    }});

    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"model matrices"}},
        .type = layer_types::BufferType::ModelBufferObject,
        .size = 10,
        .binding = layer_types::Binding {
            .name = {{"model matrices"}},
            .binding = 0,
            .type = layer_types::BindingType::Storage,
            .size = 1,
            .vertex = true,
        }
    }});

    auto const pipeline_data = createPipelineData(core, program_desc);
    auto const [pipeline, pipeline_layout] = createCascadedShadowMapPipeline(pipeline_data, core.swap_chain.extent, core.device, shadow_map.render_pass);
    shadow_map.pipeline = bindPipeline(core, pipeline_data, pipeline, pipeline_layout);

    return shadow_map;
}