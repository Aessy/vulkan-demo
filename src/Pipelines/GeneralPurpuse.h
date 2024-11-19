#include "VulkanRenderSystem.h"

#include "Program.h"
#include "Textures.h"

#include <array>

Pipeline createGeneralPurposePipeline(RenderingState const& state,
                                      vk::RenderPass const& render_pass,
                                      Textures const& textures,
                                      std::vector<UniformBuffer> const& shadow_map_buffer = {},
                                      std::array<vk::ImageView, 2> const& shadow_map_images = {{}});