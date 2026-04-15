#pragma once

#include "VulkanRenderSystem.h"
#include "Program.h"

#include <memory>
#include <vector>

Pipeline createAtmospherePipeline(RenderingState const& state,
                                  vk::RenderPass const& render_pass,
                                  std::vector<std::unique_ptr<UniformBuffer>> const& world_buffer,
                                  std::vector<std::unique_ptr<UniformBuffer>> const& model_buffer,
                                  std::vector<std::unique_ptr<UniformBuffer>> const& atmosphere_color_buffer);
