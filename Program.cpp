#include "Program.h"

#include "VulkanRenderSystem.h"
#include "utilities.h"
#include "Textures.h"

DescriptionPoolAndSet createDescriptorSet(vk::Device const& device, vk::DescriptorSetLayout const& layout,
                                          std::vector<vk::DescriptorSetLayoutBinding> const& layout_bindings)
{
    auto pool = createDescriptorPoolInfo(device, layout_bindings);
    auto set = createSets(device, pool, layout, layout_bindings);

    return DescriptionPoolAndSet
    {
        pool,
        set,
        layout,
        layout_bindings
    };
}

GpuProgram createGpuProgram(std::vector<std::vector<vk::DescriptorSetLayoutBinding>> descriptor_set_layout_bindings, RenderingState const& rendering_state,
                                                                            std::string const& shader_vert, std::string const& shader_frag)
{
    // Create layouts for the descriptor sets
    std::vector<vk::DescriptorSetLayout> descriptor_set_layouts;
    std::transform(descriptor_set_layout_bindings.begin(), descriptor_set_layout_bindings.end(), std::back_inserter(descriptor_set_layouts),
            [&rendering_state](auto const& set){return createDescriptorSetLayout(rendering_state.device, set);});

    // Create the default pipeline
    auto const graphic_pipeline = createGraphicsPipline(rendering_state.device, rendering_state.swap_chain.extent, rendering_state.render_pass, descriptor_set_layouts, readFile(shader_vert), readFile(shader_frag));

    // Create descriptor set for the textures, lights, and matrices
    int i = 0;
    std::vector<DescriptionPoolAndSet> desc_sets;
    for (auto const& descriptor_set_layout : descriptor_set_layouts)
    {
        std::cout << "Creating set\n";
        desc_sets.push_back(createDescriptorSet(rendering_state.device, descriptor_set_layout, descriptor_set_layout_bindings[i++]));
    }

    std::cout << "Finished creating GPU program\n";

    return { graphic_pipeline.first, graphic_pipeline.second, desc_sets};
};

std::unique_ptr<Program> createProgram(layer_types::Program const& program_data, RenderingState const& core, Textures const& textures)
{
    namespace lt = layer_types;

    std::vector<std::vector<vk::DescriptorSetLayoutBinding>> descriptor_set_layout_bindings;

    for (auto const& buffer : program_data.buffers)
    {
        auto const& binding = buffer.binding;

        vk::ShaderStageFlags shader_flags {};
        if (binding.fragment) shader_flags |= vk::ShaderStageFlagBits::eFragment;
        if (binding.vertex) shader_flags |= vk::ShaderStageFlagBits::eVertex;


        switch(binding.type)
        {
            case lt::BindingType::TextureSampler:
                descriptor_set_layout_bindings.push_back({createTextureSamplerBinding(binding.binding, binding.size,shader_flags)});
                break;
            case lt::BindingType::Uniform:
                descriptor_set_layout_bindings.push_back({createUniformBinding(binding.binding, binding.size,shader_flags)});
                break;
            case lt::BindingType::Storage:
                descriptor_set_layout_bindings.push_back({createStorageBufferBinding(binding.binding, binding.size,shader_flags)});
                break;
        };
    }

    std::string vertex_path = program_data.vertex_shader.data();
    std::string fragment_path = program_data.fragment_shader.data();

    auto program = createGpuProgram(descriptor_set_layout_bindings, core, vertex_path, fragment_path);

    std::vector<buffer_types::ModelType> model_types;

    size_t index = 0;
    for (auto const& buffer : program_data.buffers)
    {
        switch(buffer.binding.type)
        {
            case lt::BindingType::TextureSampler:
            {
                updateImageSampler(core.device, textures.textures, program.descriptor_sets[index].set, program.descriptor_sets[index].layout_bindings[0]);
                break;
            }
            case lt::BindingType::Uniform:
            {
                std::vector<UniformBuffer> buffers;
                if (buffer.type == lt::BufferType::ModelBufferObject)
                {
                    buffers = createUniformBuffers<ModelBufferObject>(core);
                    updateUniformBuffer<ModelBufferObject>(core.device, buffers, program.descriptor_sets[index].set, program.descriptor_sets[index].layout_bindings[0], buffer.size);
                    model_types.push_back(buffer_types::Model{buffers});
                }
                else if (buffer.type == lt::BufferType::WorldBufferObject)
                {
                    buffers = createUniformBuffers<WorldBufferObject>(core);
                    updateUniformBuffer<WorldBufferObject>(core.device, buffers, program.descriptor_sets[index].set, program.descriptor_sets[index].layout_bindings[0], buffer.size);
                    model_types.push_back(buffer_types::World{buffers});
                }
                break;
            }
            case lt::BindingType::Storage:
            {
                std::vector<UniformBuffer> buffers;
                if (buffer.type == lt::BufferType::ModelBufferObject)
                {
                    auto buffers = createStorageBuffers<ModelBufferObject>(core, buffer.size);
                    updateUniformBuffer<ModelBufferObject>(core.device, buffers, program.descriptor_sets[index].set, program.descriptor_sets[index].layout_bindings[0], buffer.size);
                    model_types.push_back(buffer_types::Model{buffers});
                }
                else if (buffer.type == lt::BufferType::WorldBufferObject)
                {
                    auto buffers = createStorageBuffers<WorldBufferObject>(core, buffer.size);
                    updateUniformBuffer<WorldBufferObject>(core.device, buffers, program.descriptor_sets[index].set, program.descriptor_sets[index].layout_bindings[0], buffer.size);
                    model_types.push_back(buffer_types::World{buffers});
                }
                break;
            }

        };

        ++index;
    }

    return std::make_unique<Program>(0,std::move(program),std::move(model_types));
}