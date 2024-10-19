#include "Program.h"

#include "Model.h"
#include "VulkanRenderSystem.h"
#include "descriptor_set.h"
#include "utilities.h"
#include "Textures.h"
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>

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
                                                                            vk::RenderPass const& render_pass,
                                                                            std::string const& shader_vert, 
                                                                            std::string const& shader_frag,
                                                                            std::string const& tess_ctrl,
                                                                            std::string const& tess_evu,
                                                                            std::string const& compute,
                                                                            vk::PolygonMode polygon_mode)
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
    if (compute.size())
    {
        ShaderStage s {
            .module = createShaderModule(readFile(compute), rendering_state.device),
            .stage = vk::ShaderStageFlagBits::eCompute
        };

        shader_stages.push_back(s);
    }

    std::pair<std::vector<vk::Pipeline>, vk::PipelineLayout> graphic_pipeline;
    if (compute.size())
    {
        graphic_pipeline = createComputePipeline(rendering_state.device, shader_stages[0], descriptor_set_layouts);
    }
    else
    {
        // Create the default pipeline
        graphic_pipeline = createGraphicsPipline(rendering_state.device, rendering_state.swap_chain.extent, render_pass, descriptor_set_layouts, shader_stages, rendering_state.msaa, polygon_mode);
    }

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

std::unique_ptr<Program> createProgram(layer_types::Program const& program_data, RenderingState const& core, Textures const& textures, vk::RenderPass const& render_pass)
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
        if (binding.compute) shader_flags |= vk::ShaderStageFlagBits::eCompute;


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
            case lt::BindingType::StorageImage:
                descriptor_set_layout_bindings.push_back({createStorageImageBinding(binding.binding, binding.size, shader_flags)});
        };
    }

    std::string vertex_path = program_data.vertex_shader.data();
    std::string fragment_path = program_data.fragment_shader.data();
    std::string tess_ctrl_path = program_data.tesselation_ctrl_shader.data();
    std::string tess_evu_path = program_data.tesselation_evaluation_shader.data();
    std::string compute_path = program_data.compute_shader.data();

    vk::PolygonMode polygon_mode = vk::PolygonMode::eFill;
    switch(program_data.polygon_mode)
    {
        case lt::PolygonMode::Fill:
            polygon_mode = vk::PolygonMode::eFill;
            break;
        case lt::PolygonMode::Line:
            polygon_mode = vk::PolygonMode::eLine;
            break;
    };

    auto program = createGpuProgram(descriptor_set_layout_bindings, core, render_pass, vertex_path, fragment_path, tess_ctrl_path, tess_evu_path, compute_path, polygon_mode);

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
            case lt::BufferType::FogVolumeObject:
                model_types.push_back(createModel<FogVolumeBufferObject, buffer_types::FogVolume>(core, buffer.binding.type, program, index, buffer.size));
                break;
            case lt::BufferType::NoBuffer:
                if (buffer.binding.type == lt::BindingType::TextureSampler)
                {
                    updateImageSampler(core.device, textures.textures, program.descriptor_sets[index].set, program.descriptor_sets[index].layout_bindings[0]);
                }
                else if (buffer.binding.type == lt::BindingType::StorageImage && buffer.binding.storage_image != vk::ImageView(VK_NULL_HANDLE))
                {
                    updateImage(core.device, buffer.binding.storage_image, program.descriptor_sets[index].set, program.descriptor_sets[index].layout_bindings[0]);
                }
                break;
        };

        ++index;
    }

    return std::make_unique<Program>(0,std::move(program),std::move(model_types));
}
