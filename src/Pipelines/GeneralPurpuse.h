#include "VulkanRenderSystem.h"

#include "Program.h"
#include "Textures.h"

Pipeline createGeneralPurposePipeline(RenderingState const& state, vk::RenderPass const& render_pass, Textures const& textures);