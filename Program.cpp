#include "Program.h"

#include "VulkanRenderSystem.h"
#include "utilities.h"
#include "Textures.h"
#include <vulkan/vulkan_enums.hpp>

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
                                                                            std::string const& shader_vert, std::string const& shader_frag, std::string const& tess_ctrl, std::string const& tess_evu)
{
    // Create layouts for the descriptor sets
    std::vector<vk::DescriptorSetLayout> descriptor_set_layouts;
    std::transform(descriptor_set_layout_bindings.begin(), descriptor_set_layout_bindings.end(), std::back_inserter(descriptor_set_layouts),
            [&rendering_state](auto const& set){return createDescriptorSetLayout(rendering_state.device, set);});


    std::vector<ShaderStage> shader_stages;
    if (shader_vert.size())
    {
        ShaderStage s {
            .module = createShaderModule(readFile(shader_vert), rendering_state.device),
            .stage = vk::ShaderStageFlagBits::eVertex
        };

        shader_stages.push_back(s);
    }
    if (shader_frag.size())
    {
        ShaderStage s {
            .module = createShaderModule(readFile(shader_frag), rendering_state.device),
            .stage = vk::ShaderStageFlagBits::eFragment
        };

        shader_stages.push_back(s);
    }
    if (tess_ctrl.size())
    {
        ShaderStage s {
            .module = createShaderModule(readFile(tess_ctrl), rendering_state.device),
            .stage = vk::ShaderStageFlagBits::eTessellationControl
        };

        shader_stages.push_back(s);
    }
    if (tess_evu.size())
    {
        ShaderStage s {
            .module = createShaderModule(readFile(tess_evu), rendering_state.device),
            .stage = vk::ShaderStageFlagBits::eTessellationEvaluation
        };

        shader_stages.push_back(s);
    }

    // Create the default pipeline
    auto const graphic_pipeline = createGraphicsPipline(rendering_state.device, rendering_state.swap_chain.extent, rendering_state.render_pass, descriptor_set_layouts, shader_stages, rendering_state.msaa);

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

template<typename BufferType, typename BufferTypeOut>
auto createModel(RenderingState const& core, layer_types::BindingType binding_type, GpuProgram const& program, size_t index, int size)
{
    std::vector<UniformBuffer> buffers;
    if (binding_type == layer_types::BindingType::Uniform)
    {
        buffers = createUniformBuffers<BufferType>(core);
    }
    else if (binding_type == layer_types::BindingType::Storage)
    {
        buffers = createStorageBuffers<BufferType>(core, size);
    }
    if (binding_type != layer_types::BindingType::TextureSampler)
    {
        updateUniformBuffer<BufferType>(core.device, buffers, program.descriptor_sets[index].set, program.descriptor_sets[index].layout_bindings[0], size);
    }

    return BufferTypeOut{buffers};
}

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
        if (binding.tess_ctrl) shader_flags |= vk::ShaderStageFlagBits::eTessellationControl;
        if (binding.tess_evu) shader_flags |= vk::ShaderStageFlagBits::eTessellationEvaluation;


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
    std::string tess_ctrl_path = program_data.tesselation_ctrl_shader.data();
    std::string tess_evu_path = program_data.tesselation_evaluation_shader.data();

    auto program = createGpuProgram(descriptor_set_layout_bindings, core, vertex_path, fragment_path, tess_ctrl_path, tess_evu_path);

    std::vector<buffer_types::ModelType> model_types;

    size_t index = 0;
    for (auto const& buffer : program_data.buffers)
    {
        switch (buffer.type)
        {
            case lt::BufferType::ModelBufferObject:
                model_types.push_back(createModel<ModelBufferObject, buffer_types::Model>(core, buffer.binding.type, program, index, buffer.size));
                break;
            case lt::BufferType::WorldBufferObject:
                model_types.push_back(createModel<WorldBufferObject, buffer_types::World>(core, buffer.binding.type, program, index, buffer.size));
                break;
            case lt::BufferType::TerrainBufferObject:
                model_types.push_back(createModel<TerrainBufferObject, buffer_types::Terrain>(core, buffer.binding.type, program, index, buffer.size));
                break;
            case lt::BufferType::NoBuffer:
                if (buffer.binding.type == lt::BindingType::TextureSampler)
                    updateImageSampler(core.device, textures.textures, program.descriptor_sets[index].set, program.descriptor_sets[index].layout_bindings[0]);
                break;
        };

        ++index;
    }

    return std::make_unique<Program>(0,std::move(program),std::move(model_types));
}