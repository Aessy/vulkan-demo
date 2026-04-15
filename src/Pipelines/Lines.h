#pragma once

#include "VulkanRenderSystem.h"
#include "Program.h"

#include <vector>
#include <memory>

Pipeline createLinesPipeline(RenderingState const& state,
                             vk::RenderPass const& render_pass,
                             std::vector<std::unique_ptr<UniformBuffer>> const& world_buffer,
                             std::vector<std::unique_ptr<UniformBuffer>> const& model_buffer);
