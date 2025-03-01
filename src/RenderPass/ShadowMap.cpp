#include "ShadowMap.h"
#include "Model.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/glm.hpp>

#include "TypeLayer.h"
#include "VulkanRenderSystem.h"
#include "Program.h"
#include "Scene.h"
#include "Renderer.h"
#include "descriptor_set.h"

#include <glm/matrix.hpp>
#include <limits>
#include <vector>

#include <spdlog/spdlog.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

static const size_t shadow_map_dim = 4096;

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

    constexpr float z_mult = 7;
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

    glm::mat4 const light_projection = glm::ortho(min_x, max_x, min_x, max_y, min_z, max_z);

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

constexpr static size_t n_cascaded_shadow_maps = 4;
constexpr static float cascadeSplitLambda = 0.95f;


std::array<float, n_cascaded_shadow_maps> calculateCascadeSplits()
{
    static constexpr float near_clip = 0.5f;
    static constexpr float far_clip = 45.0f;
    static constexpr float clip_range = far_clip - near_clip;

    static constexpr float min_z = near_clip;
    static constexpr float max_z = near_clip + clip_range;

    static constexpr float range = max_z - min_z;
    static constexpr float ratio = max_z / min_z;

    std::array<float, n_cascaded_shadow_maps> cascaded_splits;

    for (uint32_t i = 0; i < n_cascaded_shadow_maps; i++) {
        float p = (i + 1) / static_cast<float>(n_cascaded_shadow_maps);
        float log = min_z * std::pow(ratio, p);
        float uniform = min_z + range * p;
        float d = cascadeSplitLambda * (log - uniform) + uniform;
        cascaded_splits[i] = (d - near_clip) / clip_range;
    }

    return cascaded_splits;
}

std::vector<std::pair<glm::mat4, float>> updateCascades(Camera const& camera, glm::vec3 const& light_dir)
{
    std::vector<std::pair<glm::mat4, float>> out;

    auto const cascade_splits = calculateCascadeSplits();
    auto const camera_view = glm::lookAt(camera.pos, (camera.pos + camera.camera_front), camera.up);

    static constexpr float near_clip = 0.5f;
    static constexpr float far_clip = 45.0f;
    static constexpr float clip_range = far_clip - near_clip;

    float last_split_dist = 0.0f;

    for (uint32_t i = 0; i < n_cascaded_shadow_maps; ++i)
    {
        float split_dist = cascade_splits[i];
        glm::vec3 frustum_corners[8] = {
            glm::vec3(-1.0f,  1.0f, 0.0f),
            glm::vec3( 1.0f,  1.0f, 0.0f),
            glm::vec3( 1.0f, -1.0f, 0.0f),
            glm::vec3(-1.0f, -1.0f, 0.0f),
            glm::vec3(-1.0f,  1.0f,  1.0f),
            glm::vec3( 1.0f,  1.0f,  1.0f),
            glm::vec3( 1.0f, -1.0f,  1.0f),
            glm::vec3(-1.0f, -1.0f,  1.0f),
        };

        glm::mat4 inv_cam = glm::inverse(camera.proj * camera_view);
        for (uint32_t corner = 0; corner < 8; ++corner)
        {
            glm::vec4 inv_corner = inv_cam * glm::vec4(frustum_corners[corner], 1.0f);
            frustum_corners[corner] = inv_corner / inv_corner.w;
        }

        for (uint32_t corner = 0; corner < 4; ++corner)
        {
            glm::vec3 dist = frustum_corners[corner+4] - frustum_corners[corner];
            frustum_corners[corner+4] = frustum_corners[corner] + (dist * split_dist);
            frustum_corners[corner] = frustum_corners[corner] + (dist * last_split_dist);
        }

        glm::vec3 frustum_center = glm::vec3(0.0f);
        for (uint32_t corner = 0; corner < 8; ++corner)
        {
            frustum_center += frustum_corners[corner];
        }
        frustum_center /= 8.0f;

        float radius = 0.0f;
        for (uint32_t corner = 0; corner < 8; ++corner)
        {
            float const distance = glm::length(frustum_corners[corner] - frustum_center);
            radius = glm::max(radius, distance);
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;

        glm::vec3 max_extents = glm::vec3(radius);
        glm::vec3 min_extents = -max_extents;

        //TODO: Check light dir orientation
        glm::mat4 light_view_matrix = glm::lookAt(frustum_center - light_dir * -min_extents.z, frustum_center, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 light_ortho_matrix = glm::ortho(min_extents.x, max_extents.x, min_extents.y, max_extents.y, 0.0f, max_extents.z - min_extents.z);

        glm::mat4 out_matrix = light_ortho_matrix * light_view_matrix;
        float dist = (near_clip + split_dist * clip_range) * -1.0f;

        out.push_back({out_matrix, dist});
    }

    return out;
}

static constexpr size_t SHADOW_MAP_CASCADE_COUNT = 4;

struct Cascade
{
    glm::mat4 viewProjMatrix;
    float splitDepth;
};

std::array<Cascade, 4> updateCascadesOriginal(Camera const& camera, glm::vec3 const& light_dir)
{
    auto const camera_view = glm::lookAt(camera.pos, (camera.pos + camera.camera_front), camera.up);
    auto const camera_proj = glm::perspective(glm::radians(45.0f), 1920.0f / 1080.0f, 0.5f, 45.0f);

    float cascadeSplits[SHADOW_MAP_CASCADE_COUNT];

    float nearClip = 0.05f;
    float farClip = 45.0f;
    float clipRange = farClip - nearClip;

    float minZ = nearClip;
    float maxZ = nearClip + clipRange;

    float range = maxZ - minZ;
    float ratio = maxZ / minZ;

    // Calculate split depths based on view camera frustum
    // Based on method presented in https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
    for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; i++) {
        float p = (i + 1) / static_cast<float>(SHADOW_MAP_CASCADE_COUNT);
        float log = minZ * std::pow(ratio, p);
        float uniform = minZ + range * p;
        float d = cascadeSplitLambda * (log - uniform) + uniform;
        cascadeSplits[i] = (d - nearClip) / clipRange;
    }

    std::array<Cascade, SHADOW_MAP_CASCADE_COUNT> cascades;

    // Calculate orthographic projection matrix for each cascade
    float lastSplitDist = 0.0;
    for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; i++) {
        float splitDist = cascadeSplits[i];

        glm::vec3 frustumCorners[8] = {
            glm::vec3(-1.0f,  1.0f, 0.0f),
            glm::vec3( 1.0f,  1.0f, 0.0f),
            glm::vec3( 1.0f, -1.0f, 0.0f),
            glm::vec3(-1.0f, -1.0f, 0.0f),
            glm::vec3(-1.0f,  1.0f,  1.0f),
            glm::vec3( 1.0f,  1.0f,  1.0f),
            glm::vec3( 1.0f, -1.0f,  1.0f),
            glm::vec3(-1.0f, -1.0f,  1.0f),
        };

        // Project frustum corners into world space
        glm::mat4 invCam = glm::inverse(camera_proj * camera_view);
        for (uint32_t j = 0; j < 8; j++) {
            glm::vec4 invCorner = invCam * glm::vec4(frustumCorners[j], 1.0f);
            frustumCorners[j] = invCorner / invCorner.w;
        }

        for (uint32_t j = 0; j < 4; j++) {
            glm::vec3 dist = frustumCorners[j + 4] - frustumCorners[j];
            frustumCorners[j + 4] = frustumCorners[j] + (dist * splitDist);
            frustumCorners[j] = frustumCorners[j] + (dist * lastSplitDist);
        }

        // Get frustum center
        glm::vec3 frustumCenter = glm::vec3(0.0f);
        for (uint32_t j = 0; j < 8; j++) {
            frustumCenter += frustumCorners[j];
        }
        frustumCenter /= 8.0f;

        float radius = 0.0f;
        for (uint32_t j = 0; j < 8; j++) {
            float distance = glm::length(frustumCorners[j] - frustumCenter);
            radius = glm::max(radius, distance);
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;

        glm::vec3 maxExtents = glm::vec3(radius);
        glm::vec3 minExtents = -maxExtents;

        float texel_size = 2.0f * radius / shadow_map_dim;

        minExtents = floor(minExtents / texel_size) * texel_size;
        maxExtents = floor(maxExtents / texel_size) * texel_size;

        // Set camera at frustum center and look along the light dir
        glm::mat4 tmp_light_view = glm::lookAt(frustumCenter, frustumCenter+light_dir, glm::vec3(0,1,0));

        // Transform furstum center into light view space
        glm::vec3 frustum_center_light_space = glm::vec3(tmp_light_view * glm::vec4(frustumCenter, 1.0f));
        
        // Snap the furstum center to texel size
        glm::vec3 snapped_center_in_light_space = glm::floor(frustum_center_light_space / texel_size) * texel_size;

        // Not necessary. Can fix later
        // glm::vec3 offset_lightspace = floor(glm::vec3(0.0f, 0.0f, -minExtents.z) / texel_size) * texel_size;

        // Calculate the final camera position in lightspace
        // glm ::vec3 camera_position_in_lightspace = snapped_center_in_light_space + offset_lightspace;

        // Transform the smapped position back
        // glm::vec3 camera_position = glm::inverse(tmp_light_view) * glm::vec4(camera_position_in_lightspace, 1.0f);
        // glm::vec3 snapped_center = glm::inverse(tmp_light_view) * glm::vec4(snapped_center_in_light_space, 1.0f);


        glm::vec3 snapped_center = floor(frustumCenter / texel_size) * texel_size;


        glm::vec3 offset = floor((-light_dir * -minExtents.z) / texel_size) * texel_size;

        // glm::vec3 const camera_position = snapped_center + offset;
        glm::mat4 lightViewMatrix = glm::lookAt(snapped_center+offset, snapped_center, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightOrthoMatrix = glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, minExtents.z*3, maxExtents.z - minExtents.z);

        if (i == 3)
        {
            spdlog::info("Texel size: {}", texel_size);
            spdlog::info("Min Extents: {}", glm::to_string(minExtents));
            spdlog::info("Max Extents: {}", glm::to_string(maxExtents));
            spdlog::info("Snapped Center: {}", glm::to_string(snapped_center));
            //spdlog::info("Camera position: {}", glm::to_string(camera_position));
        }


/*
        assert(glm::mod(snapped_center.x, texel_size) == 0.0f);
        assert(glm::mod(snapped_center.y, texel_size) == 0.0f);
        assert(glm::mod(snapped_center.z, texel_size) == 0.0f);

        assert(glm::mod(offset.x, texel_size) == 0.0f);
        assert(glm::mod(offset.y, texel_size) == 0.0f);
        assert(glm::mod(offset.z, texel_size) == 0.0f);

        assert(glm::mod(minExtents.x, texel_size) == 0.0f);
        assert(glm::mod(minExtents.y, texel_size) == 0.0f);
        assert(glm::mod(minExtents.z, texel_size) == 0.0f);

        assert(glm::mod(maxExtents.x, texel_size) == 0.0f);
        assert(glm::mod(maxExtents.y, texel_size) == 0.0f);
        assert(glm::mod(maxExtents.z, texel_size) == 0.0f);

        assert(glm::mod(camera_position.x, texel_size) == 0.0f);
        assert(glm::mod(camera_position.y, texel_size) == 0.0f);
        assert(glm::mod(camera_position.z, texel_size) == 0.0f);
*/

        // Store split distance and matrix in cascade
        cascades[i].splitDepth = (0.05f + splitDist * clipRange) * -1.0f;
        cascades[i].viewProjMatrix = lightOrthoMatrix * lightViewMatrix;

        lastSplitDist = cascadeSplits[i];
    }

    return cascades;
}

void shadowPassWriteBuffers(RenderingState const& state, Scene const& scene, CascadedShadowMap& shadow_map, int frame)
{
    vk::Extent2D extent{shadow_map_dim, shadow_map_dim};
    glm::vec3 light_dir = glm::normalize(scene.light.sun_pos);
    auto const light_space_matrix = getLightSpaceMatrices(extent, scene.camera, light_dir);

    auto const cascades = updateCascades(scene.camera, glm::normalize(-scene.light.sun_pos));

    auto const cascades_orig = updateCascadesOriginal(scene.camera, glm::normalize(-scene.light.sun_pos));

    for (int i = 0; i < cascades.size(); ++i)
    {
        writeBuffer(*shadow_map.cascaded_shadow_map_buffer[frame], cascades_orig[i].viewProjMatrix, i, state.uniform_buffer_alignment_min);
        writeBuffer(*shadow_map.cascaded_shadow_map_buffer_packed[frame], cascades_orig[i].viewProjMatrix, i);

        writeBuffer(*shadow_map.cascaded_distances[frame], cascades_orig[i].splitDepth, i, 16);
    }
}

static uint32_t getOffset(size_t object_size, size_t alignment, size_t index)
{
    vk::DeviceSize element_size = alignment - ((object_size - 1) % alignment) + (object_size-1);
    std::size_t position = element_size * index;
    return position;
}

static void drawShadowMap(vk::CommandBuffer const& command_buffer,
                   unsigned int cascade,
                   Scene const& scene,
                   CascadedShadowMap& shadow_map,
                   unsigned int frame,
                   glm::mat4 const& light_space_matrix,
                   size_t min_uniform_alignment)
{

    uint32_t offset = getOffset(sizeof(glm::mat4), min_uniform_alignment, cascade);
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


    int index = 0;
    for (auto const& o : scene.programs)
    {
        for (size_t i = 0; i < o.second.size(); ++i)
        {
            auto &drawable = scene.objs[o.second[i]];
            if (drawable.shadow)
            {
                command_buffer.bindVertexBuffers(0, drawable.vertex_buffer, {0});
                command_buffer.bindIndexBuffer(drawable.index_buffer, 0, vk::IndexType::eUint32);
                command_buffer.drawIndexed(drawable.indices_size, 1, 0,0, i);
            }
            index++;
        }
    }
}

void shadowMapRenderPass(RenderingState const& state, CascadedShadowMap& shadow_map, Scene const& scene, vk::raii::CommandBuffer const& command_buffer)
{

    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                shadow_map.pipeline.pipeline);

    for (int i = 0; i < shadow_map.framebuffer_data.framebuffers[state.current_frame].size(); ++i)
    {
        vk::Framebuffer current_framebuffer = *shadow_map.framebuffer_data.framebuffers[state.current_frame][i];

        std::array<vk::ClearValue, 2> clear_values{};
        clear_values[0].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);

        vk::Viewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(shadow_map_dim);
        viewport.height = static_cast<float>(shadow_map_dim);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor{};
        scissor.offset = vk::Offset2D{0,0};
        scissor.extent = vk::Extent2D{shadow_map_dim, shadow_map_dim};

        command_buffer.setScissor(0,scissor);
        command_buffer.setViewport(0,viewport);

        vk::RenderPassBeginInfo render_pass_info{};
        render_pass_info.sType = vk::StructureType::eRenderPassBeginInfo;
        render_pass_info.setRenderPass(shadow_map.render_pass);
        render_pass_info.setFramebuffer(current_framebuffer);
        render_pass_info.setClearValues(clear_values);
        render_pass_info.renderArea.offset = vk::Offset2D(0,0);
        render_pass_info.renderArea.extent = vk::Extent2D{shadow_map_dim, shadow_map_dim};

        command_buffer.beginRenderPass(render_pass_info,
                                    vk::SubpassContents::eInline);

        drawShadowMap(command_buffer, i, scene, shadow_map, state.current_frame, glm::mat4(), state.uniform_buffer_alignment_min);

        command_buffer.endRenderPass();
    }

    //transitionImageLayout(command_buffer, shadow_map.framebuffer_data.cascade_images[state.current_frame]->depth_image, vk::Format::eD32Sfloat,
    //                    vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 1, 5);
}

static DepthResources createShadowMapDepthResource(RenderingState const& state, uint32_t n_cascades)
{
    vk::Format format = getDepthFormat();

    auto [depth_image, device_memory] = createImage(state, shadow_map_dim, shadow_map_dim, 1, format,
                            vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst,
                            vk::MemoryPropertyFlagBits::eDeviceLocal, vk::SampleCountFlagBits::e1, n_cascades);

    

    transitionImageLayout(state, depth_image, format, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal, 1);

    auto depth_image_view = createImageView(state.device, depth_image, format, vk::ImageAspectFlagBits::eDepth, 1);

    return {std::move(depth_image), std::move(device_memory), std::move(depth_image_view)};
}

static ShadowMapFramebuffer createCascadedShadowmapFramebuffers(RenderingState const& state,
                                                                vk::RenderPass const& render_pass,
                                                                unsigned int num_cascades)
{
    ShadowMapFramebuffer data;

    for (int i = 0; i < 2; ++i)
    {
        auto depth_image = createShadowMapDepthResource(state, num_cascades);
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
            framebuffer_info.width = shadow_map_dim;
            framebuffer_info.height = shadow_map_dim;
            framebuffer_info.layers = 1;

            auto result = state.device.createFramebuffer(framebuffer_info);

            data.framebuffers[i].push_back(std::make_unique<vk::raii::Framebuffer>(std::move(result.value())));
            data.attachment_image_views[i].push_back(std::make_unique<vk::raii::ImageView>(std::move(image_view)));
        }
        
        auto image_view_array = createImageView(state.device, depth_image.depth_image, getDepthFormat(),
                                                vk::ImageAspectFlagBits::eDepth, 1, vk::ImageViewType::e2DArray,
                                                num_cascades, 0);

        // Store the image and image view to the array for use in other render pass steps.
        data.cascade_images.push_back(std::make_unique<DepthResources>(std::move(depth_image)));
        data.image_views.push_back(std::make_unique<vk::raii::ImageView>(std::move(image_view_array)));
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
    depth_stencil_state.depthCompareOp = vk::CompareOp::eLessOrEqual;
    depth_stencil_state.depthBoundsTestEnable = false;
    depth_stencil_state.stencilTestEnable = false;
    depth_stencil_state.minDepthBounds = 0.0f;
    depth_stencil_state.maxDepthBounds = 1.0f;

    vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{};
    input_assembly_state.topology = vk::PrimitiveTopology::eTriangleList;
    input_assembly_state.primitiveRestartEnable = VK_FALSE;

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

    std::vector<vk::DynamicState> dynamic_states {
          vk::DynamicState::eViewport
        , vk::DynamicState::eScissor
    };

    vk::PipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = vk::StructureType::ePipelineDynamicStateCreateInfo;
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state.setDynamicStates(dynamic_states);

    vk::Viewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = shadow_map_dim;
    viewport.height= shadow_map_dim;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vk::Rect2D scissor{};
    scissor.offset = vk::Offset2D(0,0);
    scissor.extent = vk::Extent2D{shadow_map_dim, shadow_map_dim};


    vk::PipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = vk::StructureType::ePipelineViewportStateCreateInfo;
    viewport_state.setViewportCount(1);
    viewport_state.setViewports(viewport);
    viewport_state.setScissorCount(1);
    viewport_state.setScissors(scissor);


    pipeline_info.setPVertexInputState(&vertex_input_info);
    pipeline_info.setStages(stages);
    pipeline_info.setPInputAssemblyState(&input_assembly_state);
    pipeline_info.setPRasterizationState(&rasterization_state);
    pipeline_info.setPDepthStencilState(&depth_stencil_state);
    pipeline_info.setPMultisampleState(&multisampling);
    pipeline_info.setLayout(pipeline_layout.value);
    pipeline_info.setRenderPass(render_pass);
    pipeline_info.setPViewportState(&viewport_state);
    pipeline_info.setPDynamicState(&dynamic_state);
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
    depth_attachment.finalLayout = vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal;

    vk::AttachmentReference2 depth_attachment_ref{};
    depth_attachment_ref.attachment = 0;
    depth_attachment_ref.layout = vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal;

    vk::SubpassDescription2 subpass {};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;

    // TODO: Probably need fixes here
    vk::SubpassDependency2 dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;  // Previous render pass
    dependency.dstSubpass = 0;  // This post-processing subpass
    dependency.setSrcStageMask(vk::PipelineStageFlagBits::eTopOfPipe);
    dependency.setSrcAccessMask(vk::AccessFlagBits::eNone);

    dependency.dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests;
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

    // Needs alignment for when binding the descriptor set
    shadow_map.cascaded_shadow_map_buffer = createUniformBuffers<CascadedShadowMapBufferObject>(core, shadow_map.n_cascaded_shadow_maps, core.uniform_buffer_alignment_min);
    shadow_map.cascaded_shadow_map_buffer_packed = createUniformBuffers<CascadedShadowMapBufferObject>(core, shadow_map.n_cascaded_shadow_maps);

    shadow_map.cascaded_distances = createUniformBuffers<float>(core, shadow_map.n_cascaded_shadow_maps);

    // Create pipeline
    layer_types::Program program_desc;
    program_desc.fragment_shader = {{"./shaders/cascaded_shadow_frag.spv"}};
    program_desc.vertex_shader= {{"./shaders/cascaded_shadow_vert.spv"}};

    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"shadow map buffer object"}},
        .type = layer_types::BufferType::CascadedShadowMapBufferObject,
        .count = 4,
        .size = 1,
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
    auto const [pipeline_p, pipeline_layout] = createCascadedShadowMapPipeline(pipeline_data, core.swap_chain.extent, core.device, shadow_map.render_pass);
    auto const pipeline = bindPipeline(pipeline_data, pipeline_p, pipeline_layout);

    shadow_map.pipeline = pipeline;

    // Update the buffer to only indicate one element. We dynamically bind the correct position in the pipeline.
    updateUniformBuffer<CascadedShadowMapBufferObject>(core.device,
                                                       shadow_map.cascaded_shadow_map_buffer,
                                                       pipeline.descriptor_sets[0].set,
                                                       pipeline.descriptor_sets[0].layout_bindings[0],
                                                       1);

    updateUniformBuffer<WorldBufferObject>(core.device,
                                           scene.world_buffer,
                                           pipeline.descriptor_sets[1].set,
                                           pipeline.descriptor_sets[1].layout_bindings[0],
                                           1);

    updateUniformBuffer<ModelBufferObject>(core.device,
                                           scene.model_buffer,
                                           pipeline.descriptor_sets[2].set,
                                           pipeline.descriptor_sets[2].layout_bindings[0],
                                           10);

    return shadow_map;
}