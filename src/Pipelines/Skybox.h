#include "VulkanRenderSystem.h"

#include "Program.h"

Pipeline createSkyboxPipeline(RenderingState const& state,
                              vk::RenderPass const& render_pass,
                              std::vector<UniformBuffer> const& world_buffer);