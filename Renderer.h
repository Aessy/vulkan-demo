#pragma once

#include "VulkanRenderSystem.h"
#include "Scene.h"
#include "Program.h"

template<typename BufferObject>
inline void writeBuffer(UniformBuffer& dst, BufferObject const& src, size_t index = 0)
{
    void* buffer = (unsigned char*)dst.uniform_buffers_mapped+(sizeof(BufferObject)*index);
    memcpy(buffer, (unsigned char*)&src, sizeof(BufferObject));
}

inline void renderScene(vk::CommandBuffer& cmd_buffer, Scene const& scene, std::vector<std::unique_ptr<Program>>& programs, int frame)
{
    for (auto const& o : scene.objects)
    {
        auto& program = *programs[o.first].get();

        // Write buffer data (storage and uniform)
        // The model buffer has to be bigger than all the buffers it contains.
        for (auto& buffer : program.buffers)
        {
            if (auto* world = std::get_if<buffer_types::World>(&buffer))
            {
                auto ubo = createWorldBufferObject(scene);
                writeBuffer(world->buffers[frame], ubo);
            }
            else if (auto* model = std::get_if<buffer_types::Model>(&buffer))
            {
                for (size_t i = 0; i < o.second.size(); ++i)
                {
                    auto ubo = createModelBufferObject(o.second[i]);
                    writeBuffer(model->buffers[frame],ubo,i);
                }
            }
            else if (auto* model = std::get_if<buffer_types::Terrain>(&buffer))
            {
                auto ubo = createTerrainBufferObject(scene);
                writeBuffer(model->buffers[frame],ubo);
            }
        }

        cmd_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, program.program.pipeline[0]);
        for (size_t i = 0; i < program.program.descriptor_sets.size(); ++i)
        {
            auto desc_type = program.program.descriptor_sets[i].layout_bindings[0].descriptorType;
            if (desc_type == vk::DescriptorType::eStorageBufferDynamic || desc_type == vk::DescriptorType::eUniformBufferDynamic)
            {
                uint32_t offset = 0;
                cmd_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, program.program.pipeline_layout, i, 1,&program.program.descriptor_sets[i].set[frame], 1, &offset);
            }
            else
            {
                cmd_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, program.program.pipeline_layout, i, 1,&program.program.descriptor_sets[i].set[frame], 0, nullptr);
            }
        }

        for (size_t i = 0; i < o.second.size(); ++i)
        {
            auto const& drawable = o.second[i];
            cmd_buffer.bindVertexBuffers(0, drawable.mesh.vertex_buffer, {0});
            cmd_buffer.bindIndexBuffer(drawable.mesh.index_buffer, 0, vk::IndexType::eUint32);

            cmd_buffer.drawIndexed(drawable.mesh.indices_size, 1, 0,0, i);
        }
    }
}