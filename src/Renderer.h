#pragma once

#include "VulkanRenderSystem.h"
#include "Scene.h"
#include "Program.h"

#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>

inline void runPipeline(vk::CommandBuffer const& command_buffer, Scene const& scene, Pipeline const& program, int frame)
{
    command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, program.pipeline);

    for (size_t i = 0; i < program.descriptor_sets.size(); ++i)
    {
        auto desc_type = program.descriptor_sets[i].layout_bindings[0].descriptorType;
        if (desc_type == vk::DescriptorType::eStorageBufferDynamic || desc_type == vk::DescriptorType::eUniformBufferDynamic)
        {
            uint32_t offset = 0;
            command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, program.pipeline_layout, i, 1,&program.descriptor_sets[i].set[frame], 1, &offset);
        }
        else
        {
            command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, program.pipeline_layout, i, 1,&program.descriptor_sets[i].set[frame], 0, nullptr);
        }
    }

    const uint32_t workGroupSizeX = 32;
    const uint32_t workGroupSizeY = 32;
    const uint32_t workGroupSizeZ = 32;

    command_buffer.dispatch(workGroupSizeX, workGroupSizeY, workGroupSizeZ);

}