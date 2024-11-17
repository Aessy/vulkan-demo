#pragma once

#include "VulkanRenderSystem.h"
#include "Program.h"

#include<tuple>

std::tuple<vk::Pipeline, vk::PipelineLayout> createPipeline(PipelineData const& pipeline_data,
                                                        vk::Extent2D const& swap_chain_extent,
                                                        vk::Device const& device,
                                                        vk::RenderPass const& render_pass,
                                                        vk::SampleCountFlagBits msaa,
                                                        GraphicsPipelineInput = createDefaultPipelineInput());