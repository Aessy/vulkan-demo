#pragma once

#include "VulkanRenderSystem.h"
#include "Scene.h"
#include "Program.h"

#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>

inline void runPipeline(vk::CommandBuffer const& command_buffer, Scene const& scene, std::unique_ptr<Program>& program, int frame)
{
    // Write buffer data (storage and uniform)
    // The model buffer has to be bigger than all the buffers it contains.
    for (auto& buffer : program->buffers)
    {
        if (auto* world = std::get_if<buffer_types::World>(&buffer))
        {
            auto ubo = createWorldBufferObject(scene);
            writeBuffer(world->buffers[frame], ubo);
        }
        else if (auto* fog_volume = std::get_if<buffer_types::FogVolume>(&buffer))
        {
            writeBuffer(fog_volume->buffers[frame], scene.fog);
        }
    }

    command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, program->program.pipeline[0]);

    for (size_t i = 0; i < program->program.descriptor_sets.size(); ++i)
    {
        auto desc_type = program->program.descriptor_sets[i].layout_bindings[0].descriptorType;
        if (desc_type == vk::DescriptorType::eStorageBufferDynamic || desc_type == vk::DescriptorType::eUniformBufferDynamic)
        {
            uint32_t offset = 0;
            command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, program->program.pipeline_layout, i, 1,&program->program.descriptor_sets[i].set[frame], 1, &offset);
        }
        else
        {
            command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, program->program.pipeline_layout, i, 1,&program->program.descriptor_sets[i].set[frame], 0, nullptr);
        }
    }

    const uint32_t workGroupSizeX = 32;
    const uint32_t workGroupSizeY = 32;
    const uint32_t workGroupSizeZ = 32;

    command_buffer.dispatch(workGroupSizeX, workGroupSizeY, workGroupSizeZ);

}