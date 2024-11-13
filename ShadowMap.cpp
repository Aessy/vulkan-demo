#include "Model.h"

#include <glm/glm.hpp>

#include "VulkanRenderSystem.h"

#include <limits>
#include <vector>
#include <vulkan/vulkan_enums.hpp>

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
        min_x = std::min(min_y, trf.y);
        max_x = std::max(max_y, trf.y);
        min_x = std::min(min_z, trf.z);
        max_x = std::max(max_z, trf.z);
    }

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

    glm::mat4 const light_projection = glm::ortho(min_x, max_x, min_y, max_y, min_z, max_z);

    return light_projection * light_view;
}

static glm::mat4 getLightSpaceMatrix(float const near, float const far, vk::Extent2D framebuffer_size, Camera const& camera, glm::vec3 const& light_dir)
{
    // Perspective from the camera with clipped near and far
    auto const projection = glm::perspective(glm::radians(45.0f),
                                             float(framebuffer_size.width) / float(framebuffer_size.height),
                                             near, far);

    auto const corners = getFrustumCornersWorldSpace(projection, camera.view);
    auto const center = getCenter(corners);

    auto const light_view = glm::lookAt(center + light_dir, center, glm::vec3(0.0f, 1.0f, 0.0f));

    auto const light_projection = getLightProjection(light_view, corners);
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

struct Cascadedframebuffers
{
    std::vector<DepthResources> depth_resources;
    std::vector<vk::Framebuffer> framebuffers;
};

static std::vector<vk::Framebuffer> createCascadedShadowmapFramebuffers(RenderingState const& state,
                                                                        vk::RenderPass const& render_pass,
                                                                        unsigned int num_cascades)
{
    std::vector<vk::Framebuffer> framebuffers;

    for (auto const& swap_chain_image_view : state.image_views)
    {
        auto depth_image = createDepth(state, vk::SampleCountFlagBits::e1, num_cascades);
        std::array<vk::ImageView, 1> attachments = {depth_image.depth_image_view};
        vk::FramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = vk::StructureType::eFramebufferCreateInfo;
        framebuffer_info.setRenderPass(state.render_pass);
        framebuffer_info.attachmentCount = attachments.size();
        framebuffer_info.setAttachments(attachments);
        framebuffer_info.width = state.swap_chain.extent.width;
        framebuffer_info.height = state.swap_chain.extent.height;
        framebuffer_info.layers = 1;

        auto result = state.device.createFramebuffer(framebuffer_info);
        checkResult(result);

        framebuffers.push_back(result.value);
    }

    return framebuffers;
}


static vk::RenderPass createShadowMapRenderPass(vk::Format const swap_chain_image_format,
                                                vk::Device const& device,
                                                vk::SampleCountFlagBits msaa)
{

    vk::AttachmentDescription2 depth_attachment{};
    depth_attachment.format = vk::Format::eR16Sfloat;
    depth_attachment.samples = vk::SampleCountFlagBits::e1;
    depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
    depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
    depth_attachment.stencilLoadOp =  vk::AttachmentLoadOp::eDontCare;
    depth_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depth_attachment.initialLayout = vk::ImageLayout::eUndefined;
    depth_attachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::AttachmentReference2 depth_attachment_ref;
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
    dependency.setSrcAccessMask(vk::AccessFlagBits::eShaderRead);

    dependency.dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
    dependency.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    std::array<vk::AttachmentDescription2, 1> attachments {depth_attachment};
    vk::RenderPassCreateInfo2 renderPassInfo{};
    renderPassInfo.setAttachments(attachments);
    renderPassInfo.setSubpasses(subpass);
    renderPassInfo.setDependencies(dependency);

    // Create the render pass
    auto render_pass = device.createRenderPass2(renderPassInfo);

    checkResult(render_pass.result);
    return render_pass.value;
}

struct CascadedShadowMap
{
    
};