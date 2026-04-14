#include "Planet.h"
#include "Pipeline.h"

#include "Material.h"
#include "Model.h"
#include "VulkanRenderSystem.h"
#include "Program.h"
#include "Textures.h"
#include "TypeLayer.h"
#include "descriptor_set.h"

Pipeline createPlanetPipeline(RenderingState const& state,
                              vk::RenderPass const& render_pass,
                              Textures const& textures,
                              std::vector<std::unique_ptr<UniformBuffer>> const& world_buffer,
                              std::vector<std::unique_ptr<UniformBuffer>> const& model_buffer,
                              std::vector<std::unique_ptr<UniformBuffer>> const& planet_material_buffer)
{
    layer_types::Program program_desc;
    program_desc.vertex_shader   = {{"./shaders/planet_vert.spv"}};
    program_desc.fragment_shader = {{"./shaders/planet_frag.spv"}};

    // Set 0: bindless texture array (size 32)
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"texture_buffer"}},
        .type = layer_types::BufferType::NoBuffer,
        .size = 1,
        .binding = layer_types::Binding{
            .name    = {{"binding textures"}},
            .binding = 0,
            .type    = layer_types::BindingType::TextureSampler,
            .size    = 32,
            .vertex  = true,
            .fragment = true,
        }
    }});

    // Set 1: WorldBufferObject (uniform)
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"world_buffer"}},
        .type = layer_types::BufferType::WorldBufferObject,
        .size = 1,
        .binding = layer_types::Binding{
            .name    = {{"binding world"}},
            .binding = 0,
            .type    = layer_types::BindingType::Uniform,
            .size    = 1,
            .vertex  = true,
            .fragment = true,
        }
    }});

    // Set 2: ModelBufferObject SSBO (size 20)
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"model_buffer"}},
        .type = layer_types::BufferType::ModelBufferObject,
        .size = 20,
        .binding = layer_types::Binding{
            .name    = {{"binding model"}},
            .binding = 0,
            .type    = layer_types::BindingType::Storage,
            .size    = 1,
            .vertex  = true,
            .fragment = true,
        }
    }});

    // Set 3: PlanetMaterialData SSBO (size 20)
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"planet_material_buffer"}},
        .type = layer_types::BufferType::NoBuffer,
        .size = 20,
        .binding = layer_types::Binding{
            .name    = {{"binding planet_material"}},
            .binding = 0,
            .type    = layer_types::BindingType::Storage,
            .size    = 1,
            .vertex  = false,
            .fragment = true,
        }
    }});

    auto const pipeline_data = createPipelineData(state, program_desc);
    auto const [pipeline, pipeline_layout] = createPipeline(pipeline_data,
                                                            state.swap_chain.extent,
                                                            state.device,
                                                            render_pass,
                                                            state.msaa);
    auto pipeline_finish = bindPipeline(pipeline_data, pipeline, pipeline_layout);

    updateImageSampler(state.device,
                       textures.textures,
                       pipeline_finish.descriptor_sets[0].set,
                       pipeline_finish.descriptor_sets[0].layout_bindings[0]);

    updateUniformBuffer<WorldBufferObject>(state.device,
                                          world_buffer,
                                          pipeline_finish.descriptor_sets[1].set,
                                          pipeline_finish.descriptor_sets[1].layout_bindings[0],
                                          1);

    updateUniformBuffer<ModelBufferObject>(state.device,
                                          model_buffer,
                                          pipeline_finish.descriptor_sets[2].set,
                                          pipeline_finish.descriptor_sets[2].layout_bindings[0],
                                          20);

    updateUniformBuffer<PlanetMaterialData>(state.device,
                                            planet_material_buffer,
                                            pipeline_finish.descriptor_sets[3].set,
                                            pipeline_finish.descriptor_sets[3].layout_bindings[0],
                                            20);

    return pipeline_finish;
}
