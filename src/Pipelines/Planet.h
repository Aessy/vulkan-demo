#pragma once

#include "VulkanRenderSystem.h"
#include "Program.h"
#include "Textures.h"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

// std430-compatible, 80 bytes
struct PlanetMaterialData {
    alignas(4)  int   diffuse_texture{-1};
    alignas(4)  int   normal_texture{-1};
    alignas(4)  int   has_normal_map{0};
    alignas(4)  int   has_atmosphere{0};                                 // 16 bytes
    alignas(16) glm::vec4 atmosphere_color_scale{0.4f, 0.6f, 1.0f, 0.05f}; // 32 bytes
    alignas(16) glm::vec4 albedo_color{0.5f, 0.5f, 0.5f, 1.0f};            // 48 bytes
    alignas(4)  float roughness{0.5f};
    alignas(4)  float metallic{0.0f};
    alignas(4)  float emissive{0.0f};
    alignas(4)  int   cloud_texture{-1};                                 // 64 bytes
};

Pipeline createPlanetPipeline(RenderingState const& state,
                              vk::RenderPass const& render_pass,
                              Textures const& textures,
                              std::vector<std::unique_ptr<UniformBuffer>> const& world_buffer,
                              std::vector<std::unique_ptr<UniformBuffer>> const& model_buffer,
                              std::vector<std::unique_ptr<UniformBuffer>> const& planet_material_buffer);
