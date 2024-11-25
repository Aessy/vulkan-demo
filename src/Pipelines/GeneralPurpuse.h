#include "VulkanRenderSystem.h"

#include "Program.h"
#include "Textures.h"

#include <array>

Pipeline createGeneralPurposePipeline(RenderingState const& state,
                                      vk::RenderPass const& render_pass,
                                      Textures const& textures,
                                      std::vector<std::unique_ptr<UniformBuffer>> const& world_buffer = {},
                                      std::vector<std::unique_ptr<UniformBuffer>> const& model_buffer = {},
                                      std::vector<std::unique_ptr<UniformBuffer>> const& material_buffer = {},
                                      std::vector<std::unique_ptr<UniformBuffer>> const& shadow_map_buffer = {},
                                      std::vector<std::unique_ptr<vk::raii::ImageView>> const& shadow_map_images = {},
                                      std::vector<std::unique_ptr<UniformBuffer>> const& shadow_map_distances = {});