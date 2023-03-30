#define VK_USE_PLATFORM_XLIB_KHR
#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_X11
#include "GLFW/glfw3native.h"

#define VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_ASSERT_ON_RESULT
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_funcs.hpp>
#include <vulkan/vulkan_structs.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "glm/gtx/string_cast.hpp"
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <chrono>
#include <iostream>
#include <optional>
#include <set>
#include <limits>
#include <fstream>
#include <memory>
#include <queue>
#include <iterator>

#include "descriptor_set.h"

#include "VulkanRenderSystem.h"
#include "Model.h"
#include "Mesh.h"
#include "height_map.h"


struct DescriptionPoolAndSet
{
    vk::DescriptorPool pool;
    std::vector<vk::DescriptorSet> set;

    vk::DescriptorSetLayout layout;
    std::vector<vk::DescriptorSetLayoutBinding> layout_bindings;
};

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


struct GpuProgram
{
    std::vector<vk::Pipeline> pipeline;
    vk::PipelineLayout pipeline_layout;

    std::vector<DescriptionPoolAndSet> descriptor_sets;
};

struct Draw
{
    DrawableMesh mesh;
    glm::vec3 position;
    glm::vec3 rotation;
    float scale;
    float angel;

    int texture_index;

    GpuProgram program;
};


Draw createDraw(DrawableMesh const& mesh, GpuProgram program,
        glm::vec3 const& pos = glm::vec3(0,0,0))
{
    Draw draw;
    draw.mesh = mesh;
    draw.position = pos;
    draw.rotation = glm::vec3(1,1,1);
    draw.angel = 0;
    draw.scale = 1.0f;
    draw.texture_index = 0;
    draw.program = std::move(program);

    return draw;
}

Model loadModel(std::string const& model_path)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, model_path.c_str()))
    {
        std::cout << "Failed loading model\n";
        return {};
    }

    Model model;

    for (auto const& shape : shapes)
    {
        for (auto const& index : shape.mesh.indices)
        {
            Vertex vertex;

            vertex.pos = {
                  attrib.vertices[3 * index.vertex_index + 0]
                , attrib.vertices[3 * index.vertex_index + 1]
                , attrib.vertices[3 * index.vertex_index + 2]
            };

            vertex.tex_coord = {
                  attrib.texcoords[2 * index.texcoord_index + 0]
                , 1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
            };

            vertex.normal = {
                  attrib.normals[3 * index.normal_index + 0]
                , attrib.normals[3 * index.normal_index + 1]
                , attrib.normals[3 * index.normal_index + 2]
            };

            vertex.color = {0.5f, 0.1f, 0.3f};

            model.vertices.push_back(vertex);
            model.indices.push_back(model.indices.size());
        }
    }

    return model;
}

DrawableMesh loadMesh(RenderingState const& state, Model const& model)
{
    DrawableMesh mesh;
    mesh.vertex_buffer = createVertexBuffer(state, model.vertices);
    mesh.index_buffer = createIndexBuffer(state, model.indices);
    mesh.indices_size = model.indices.size();

    return mesh;
}

DrawableMesh loadMesh(RenderingState const& state, std::vector<Vertex> const& vertices,
                                                   std::vector<uint32_t> const& indices)
{
    DrawableMesh mesh;
    mesh.vertex_buffer = createVertexBuffer(state, vertices);
    mesh.index_buffer = createIndexBuffer(state, indices);
    mesh.indices_size = indices.size();

    return mesh;
}


std::pair<vk::Image, vk::DeviceMemory> createTextureImage(RenderingState const& state, std::string const& path)
{
    int width, height, channels {};

    //auto pixels = stbi_load("textures/far.jpg", &width, &height, &channels, STBI_rgb_alpha);
    auto pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels)
    {
        std::cout << "Could not load texture\n";
    }

    vk::DeviceSize image_size = width * height * 4;
    vk::DeviceMemory staging_buffer_memory;

    auto staging_buffer = createBuffer(state, image_size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, staging_buffer_memory);

    auto data = state.device.mapMemory(staging_buffer_memory, 0, image_size, static_cast<vk::MemoryMapFlagBits>(0));
    memcpy(data.value, pixels, static_cast<size_t>(image_size));
    state.device.unmapMemory(staging_buffer_memory);
    stbi_image_free(pixels);

    auto image = createImage(state, width, height, vk::Format::eR8G8B8A8Srgb, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);

    transitionImageLayout(state, image.first, vk::Format::eR8G8B8A8Srgb, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
    copyBufferToImage(state, staging_buffer, image.first, width, height);
    transitionImageLayout(state, image.first, vk::Format::eR8G8B8A8Srgb, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

    state.device.destroyBuffer(staging_buffer);
    state.device.freeMemory(staging_buffer_memory);

    return image;
}


void loop(GLFWwindow* window)
{
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
    }
}


std::vector<char> readFile(std::string const& path)
{
    std::ifstream file(path);
    return std::vector<char>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

ModelBufferObject createModelBufferObject(Draw const& drawable)
{
    ModelBufferObject model_buffer{};

    auto rotation = glm::rotate(glm::mat4(1.0f), drawable.angel, drawable.rotation);
    auto translation = glm::translate(glm::mat4(1.0f), drawable.position);
    auto scale = glm::scale(glm::mat4(1.0f), glm::vec3(drawable.scale, drawable.scale, drawable.scale));
    model_buffer.model = translation * rotation * scale;
    model_buffer.texture_index = drawable.texture_index;

    return model_buffer;
}

WorldBufferObject updateWorldBuffer(UniformBuffer& world_buffer, Camera const& camera)
{
    WorldBufferObject ubo{};

    ubo.camera_view = glm::lookAt(camera.pos, (camera.pos + camera.camera_front), camera.up);
    ubo.camera_proj = camera.proj;
    ubo.camera_proj[1][1] *= -1;

    // Update light uniform
    glm::vec3 light_pos = glm::vec3(20,30, 20);
    ubo.light_position = light_pos;

    memcpy(world_buffer.uniform_buffers_mapped, &ubo, sizeof(ubo));

    return ubo;
}

GpuProgram createGpuProgram(std::vector<std::vector<vk::DescriptorSetLayoutBinding>> descriptor_set_layout_bindings, RenderingState const& rendering_state, std::string const& shader_vert, std::string const& shader_frag)
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

struct RenderingSystem
{
    GpuProgram program;

    std::vector<UniformBuffer> storage_buffer;
    std::vector<UniformBuffer> world_buffer;

    std::vector<Draw> drawables;
};

RenderingSystem createDefaultSystem(RenderingState const& rendering_state, std::vector<Texture> const& textures)
{
    // Demo box vertices
    std::vector<Vertex> vertices {
        //Position           // Color            // UV   // Normal
        // Front
        {{-0.5, -0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {0, 0}, {0,0,1}}, //0  0
        {{ 0.5, -0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {1, 0}, {0,0,1}}, //1  1  
        {{-0.5,  0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {0, 1}, {0,0,1}}, //2  2
        {{ 0.5,  0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {1, 1}, {0,0,1}}, //3  3
                                                          //
        // Up
        {{-0.5,  0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {0, 0}, {0,1,0}}, //2  4
        {{ 0.5,  0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {1, 0}, {0,1,0}}, //3  5
        {{-0.5,  0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {0, 1}, {0,1,0}}, //6  6
        {{ 0.5,  0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {1, 1}, {0,1,0}}, //7  7

        // Down 
        {{-0.5, -0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {0, 0}, {0,-1,0}}, //0  8
        {{ 0.5, -0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {1, 0}, {0,-1,0}}, //1  9
        {{-0.5, -0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {0, 1}, {0,-1,0}}, //4 10
        {{ 0.5, -0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {1, 1}, {0,-1,0}}, //5 11
                                                          //
        // Left
        {{-0.5, -0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {0, 0}, {-1,0,0}}, //0 12
        {{-0.5, -0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {1, 0}, {-1,0,0}}, //4 13
        {{-0.5,  0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {0, 1}, {-1,0,0}}, //2 14
        {{-0.5,  0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {1, 1}, {-1,0,0}}, //6 15
                                                          //
        // Right
        {{ 0.5, -0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {1, 0}, {1,0,0}}, //1 16
        {{ 0.5, -0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {0, 0}, {1,0,0}}, //5 17
        {{ 0.5,  0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {1, 1}, {1,0,0}}, //3 18
        {{ 0.5,  0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {0, 1}, {1,0,0}}, //7 19

        // Back
        {{-0.5, -0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {1, 0}, {0,0,-1}}, //4 20
        {{ 0.5, -0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {0, 0}, {0,0,-1}}, //5 21
        {{-0.5,  0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {1, 1}, {0,0,-1}}, //6 22
        {{ 0.5,  0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {0, 1}, {0,0,-1}}  //7 23
    };

    // Demo box indices
    const std::vector<uint32_t> indices = {
        // Front
        0, 1, 3,
        3, 2, 0,

        // Top
        7, 6, 4,
        4, 5, 7,

        // Bottom
        11, 9, 8,
        8, 10, 11,

        // Left
        12, 14, 15,
        15, 13, 12,

        // Right
        16, 17, 19,
        19, 18, 16,

        // Back
        20, 22, 23,
        23, 21, 20
    };

    // Descriptor bindings for default shader
    auto textures_descriptor_binding = createTextureSamplerBinding(0, 32, vk::ShaderStageFlagBits::eFragment);
    auto lights_descriptor_binding = createUniformBinding(0, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);
    auto mvp_descriptor_binding = createStorageBufferBinding(0, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);

    std::vector<std::vector<vk::DescriptorSetLayoutBinding>> descriptor_set_layout_bindings{{textures_descriptor_binding}, // Set 0 binding 0
                                                                                    {lights_descriptor_binding},   // Set 1 binding 0
                                                                                    {mvp_descriptor_binding}};     // Set 2 binding 0
                                                                                                                   //
    auto test_program = createGpuProgram(descriptor_set_layout_bindings, rendering_state, "./shaders/vert.spv", "./shaders/frag.spv");

    // Create uniform buffers for lights and matrices for default shader
    auto model_buffers = createStorageBuffers<ModelBufferObject>(rendering_state, 10000);
    auto world_buffers = createUniformBuffers<WorldBufferObject>(rendering_state);

    // Update the descriptor sets with the buffers and textures
    updateImageSampler(rendering_state.device, textures, test_program.descriptor_sets[0].set, test_program.descriptor_sets[0].layout_bindings[0]);
    updateUniformBuffer<WorldBufferObject>(rendering_state.device, world_buffers, test_program.descriptor_sets[1].set, test_program.descriptor_sets[1].layout_bindings[0], 1);
    updateUniformBuffer<ModelBufferObject>(rendering_state.device, model_buffers, test_program.descriptor_sets[2].set, test_program.descriptor_sets[2].layout_bindings[0], 10000);

    auto mesh = loadMesh(rendering_state, vertices, indices);

    RenderingSystem render_system{test_program, model_buffers, world_buffers, {}};


    // Create the ground using a plain
    //auto model = createFlatGround(512, 200); //loadModel("./models/plain.obj");
    auto model = createModeFromHeightMap("./textures/terrain.png", 200, 50);
    auto plain_mesh = loadMesh(rendering_state, model);
    auto drawable = createDraw(plain_mesh, test_program);
    drawable.texture_index = 4; // Dirt texture

    render_system.drawables.push_back(drawable);

    // Add 500 boxes to the scene
    for (int i = 0; i < 2; ++i)
    {
        glm::vec3 pos;
        pos.x = (rand() % 10) - 5;
        pos.y = 0.5;
        pos.z = (rand() % 40) - 40;

        auto box = createDraw(mesh, test_program, pos);
        box.texture_index = 0;

        render_system.drawables.push_back(std::move(box));
    }

    return render_system;
}

struct GrassRenderingSystem
{
    GpuProgram program;

    std::vector<UniformBuffer> world_data;
    std::vector<UniformBuffer> grass_data;
    DrawableMesh grass_mesh;
};

struct World
{
    GrassRenderingSystem grass;
    RenderingSystem default_rendering;

    Camera camera;
    LightBufferObject light;
};

float randFloat(float a, float b)
{
    return ((b - a) * ((float)rand() / RAND_MAX)) + a;
}

GrassRenderingSystem createGrass(RenderingState const& rendering_state, std::vector<Texture> const& textures)
{
    // Descriptor bindings for shaderfor shader
    auto textures_descriptor_binding = createTextureSamplerBinding(0, 32, vk::ShaderStageFlagBits::eFragment);
    auto world_buffer = createUniformBinding(0, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);
    auto positions_buffer = createStorageBufferBinding(0, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);

    std::vector<std::vector<vk::DescriptorSetLayoutBinding>> descriptor_set_layout_bindings{{textures_descriptor_binding}, // Set 0 binding 0
                                                                                            {world_buffer},                // Set 1 binding 0
                                                                                            {positions_buffer}};           // Set 2 binding 0
    auto test_program = createGpuProgram(descriptor_set_layout_bindings, rendering_state, "./shaders/grass_vert.spv", "./shaders/grass_frag.spv");

    // Create uniform buffers for lights and matrices for default shader
    auto world_data = createUniformBuffers<WorldBufferObject>(rendering_state);
    auto storage_buffer = createStorageBuffers<ModelBufferObject>(rendering_state, 1000000);

    // Update the descriptor sets with the buffers and textures
    updateImageSampler(rendering_state.device, textures, test_program.descriptor_sets[0].set, test_program.descriptor_sets[0].layout_bindings[0]);
    updateUniformBuffer<WorldBufferObject>(rendering_state.device, world_data, test_program.descriptor_sets[1].set,  test_program.descriptor_sets[1].layout_bindings[0], 1);
    updateUniformBuffer<ModelBufferObject>(rendering_state.device, storage_buffer, test_program.descriptor_sets[2].set, test_program.descriptor_sets[2].layout_bindings[0], 1000000);

    // Load mesh
    auto model = loadModel("models/grass.obj");
    auto mesh = loadMesh(rendering_state, model);

    // Set positions for instanced rendering
    int i = 0;
    for (int x = 0; x < 1000; ++x)
    {
        for (int y = 0; y < 1000; ++y)
        {
            auto drawable = createDraw(mesh, test_program, glm::vec3(x/20.0f,0,y/20.0f));
            drawable.rotation = glm::vec3(0,1,0);
            drawable.angel = randFloat(0,360);
            drawable.position.x += randFloat(0.0f, 0.05f) - 0.025f;
            drawable.position.z += randFloat(0.0f, 0.05f) - 0.025f;
            drawable.position.y -= randFloat(0.0f, 0.10) - 0.05f;
            drawable.texture_index = 0;

            auto ubo = createModelBufferObject(drawable);

            for (int frame = 0; frame < 2; ++frame)
            {
                void* buffer = storage_buffer[frame].uniform_buffers_mapped;
                auto* b = (unsigned char*)buffer+(sizeof(ModelBufferObject)*i);
                memcpy(b, &ubo, sizeof(ubo));
            }

            ++i;
        }
    }

    return GrassRenderingSystem{test_program, world_data, storage_buffer, mesh};
}

void draw(vk::CommandBuffer& command_buffer, GrassRenderingSystem& render_system, Camera const& camera, int frame)
{
    updateWorldBuffer(render_system.world_data[frame], camera);

    uint32_t offset = 0;
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, render_system.program.pipeline[0]);
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, render_system.program.pipeline_layout, 0, 1, &render_system.program.descriptor_sets[0].set[frame], 0, nullptr);
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, render_system.program.pipeline_layout, 1, 1, &render_system.program.descriptor_sets[1].set[frame], 1, &offset);
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, render_system.program.pipeline_layout, 2, 1, &render_system.program.descriptor_sets[2].set[frame], 0, nullptr);

    command_buffer.bindVertexBuffers(0,render_system.grass_mesh.vertex_buffer,{0});
    command_buffer.bindIndexBuffer(render_system.grass_mesh.index_buffer, 0, vk::IndexType::eUint32);

    command_buffer.drawIndexed(render_system.grass_mesh.indices_size, 1000000,0,0,0);
}

void draw(vk::CommandBuffer& command_buffer, RenderingSystem& render_system, Camera const& camera, int frame)
{
    uint32_t offset = 0;
    int i = 0;
    for (auto const& drawable : render_system.drawables)
    {
        auto ubo = createModelBufferObject(drawable);
        void* buffer = render_system.storage_buffer[frame].uniform_buffers_mapped;
        auto* b = (unsigned char*)buffer+(sizeof(ModelBufferObject)*i);
        memcpy(b, &ubo, sizeof(ubo));
        ++i;
    }

    updateWorldBuffer(render_system.world_buffer[frame], camera);

    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, render_system.program.pipeline[0]);
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, render_system.program.pipeline_layout, 0, 1, &render_system.program.descriptor_sets[0].set[frame], 0, nullptr);
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, render_system.program.pipeline_layout, 1, 1, &render_system.program.descriptor_sets[1].set[frame], 1, &offset);
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, render_system.program.pipeline_layout, 2, 1, &render_system.program.descriptor_sets[2].set[frame], 0, nullptr);
    i = 0;

    for (auto const& drawable : render_system.drawables)
    {
        command_buffer.bindVertexBuffers(0,drawable.mesh.vertex_buffer,{0});
        command_buffer.bindIndexBuffer(drawable.mesh.index_buffer, 0, vk::IndexType::eUint32);

        command_buffer.drawIndexed(drawable.mesh.indices_size, 1, 0, 0, i++);
    }
}

void draw(vk::CommandBuffer& command_buffer, World& render_system, Camera const& camera, int frame)
{
    draw(command_buffer, render_system.default_rendering, camera, frame);
    draw(command_buffer, render_system.grass, camera, frame);
}

template<typename RenderingSystem>
void recordCommandBuffer(RenderingState& state, uint32_t image_index, RenderingSystem& render_system)
{
    auto& command_buffer = state.command_buffer[state.current_frame];

    vk::CommandBufferBeginInfo begin_info{};
    begin_info.sType = vk::StructureType::eCommandBufferBeginInfo;
    begin_info.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    begin_info.pInheritanceInfo = nullptr;

    auto result = command_buffer.begin(begin_info);

    //auto desc_set = state.descriptor_sets[0].set[0];
    //auto desc_set_2 = state.descriptor_sets[0].set[1];



    vk::RenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = vk::StructureType::eRenderPassBeginInfo;
    render_pass_info.setRenderPass(state.render_pass);
    render_pass_info.setFramebuffer(state.framebuffers[image_index]);
    render_pass_info.renderArea.offset = vk::Offset2D{0,0};
    render_pass_info.renderArea.extent = state.swap_chain.extent;

    std::array<vk::ClearValue, 2> clear_values{};
    clear_values[0].color = vk::ClearColorValue(std::array<float,4>{0,0,0,1});
    clear_values[1].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);

    render_pass_info.clearValueCount = clear_values.size();
    render_pass_info.setClearValues(clear_values);


    vk::Viewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(state.swap_chain.extent.width);
    viewport.height = static_cast<float>(state.swap_chain.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    command_buffer.setViewport(0,viewport);
    
    vk::Rect2D scissor{};
    scissor.offset = vk::Offset2D{0,0};
    scissor.extent = state.swap_chain.extent;
    command_buffer.setScissor(0,scissor);

    command_buffer.beginRenderPass(&render_pass_info, vk::SubpassContents::eInline);
    draw(command_buffer, render_system, state.camera, state.current_frame);
    command_buffer.endRenderPass();

    result = command_buffer.end();
}

enum class DrawResult
{
    SUCCESS,
    RESIZE,
    EXIT
};

template<typename RenderingSystem>
DrawResult drawFrame(RenderingState& state, RenderingSystem& render_system)
{
    auto v = state.device.waitForFences(state.semaphores.in_flight_fence[state.current_frame], true, ~0);

    auto next_image = state.device.acquireNextImageKHR(state.swap_chain.swap_chain, ~0,
            state.semaphores.image_available_semaphore[state.current_frame], VK_NULL_HANDLE);
    if (   next_image.result == vk::Result::eErrorOutOfDateKHR
        || next_image.result == vk::Result::eSuboptimalKHR)
    {
        return DrawResult::RESIZE;
    }
    else if (   next_image.result != vk::Result::eSuccess
             && next_image.result != vk::Result::eSuboptimalKHR)
    {
        std::cout << "Failed to acquire swap chain image\n";
        return DrawResult::EXIT;
    }

    state.device.resetFences(state.semaphores.in_flight_fence[state.current_frame]);

    state.command_buffer[state.current_frame].reset(static_cast<vk::CommandBufferResetFlags>(0));

    auto image_index = next_image.value;
    recordCommandBuffer(state, image_index, render_system);

    vk::PipelineStageFlags wait_stages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};

    vk::SubmitInfo submit_info{};
    submit_info.sType = vk::StructureType::eSubmitInfo;
    submit_info.setWaitSemaphores(state.semaphores.image_available_semaphore[state.current_frame]);
    submit_info.setWaitDstStageMask(wait_stages);
    submit_info.commandBufferCount = 1;
    submit_info.setCommandBuffers(state.command_buffer[state.current_frame]);
    submit_info.setSignalSemaphores(state.semaphores.render_finished_semaphore[state.current_frame]);


    auto submit_result = state.graphics_queue.submit(submit_info, state.semaphores.in_flight_fence[state.current_frame]);

    vk::PresentInfoKHR present_info{};
    present_info.sType = vk::StructureType::ePresentInfoKHR;
    present_info.setWaitSemaphores(state.semaphores.render_finished_semaphore[state.current_frame]);
    present_info.setResults(nullptr);
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &state.swap_chain.swap_chain;
    present_info.setImageIndices(image_index);

    auto result = state.present_queue.presentKHR(present_info);

    if (   result == vk::Result::eErrorOutOfDateKHR
        || result == vk::Result::eSuboptimalKHR
        || state.app->window_resize)
    {
        state.app->window_resize = false;
        return DrawResult::RESIZE;
    }

    return DrawResult::SUCCESS;
}


Texture createTexture(RenderingState const& state, std::string const& path, vk::Sampler sampler)
{
    auto image = createTextureImage(state, path);
    auto image_view = createTextureImageView(state, image.first);

    return Texture {
        .image = image.first,
        .memory = image.second,
        .view = image_view,
        .sampler = sampler
    };
}

std::vector<Texture> loadTextures(RenderingState const& state, vk::Sampler const& sampler, std::vector<std::string> const& paths)
{
    std::vector<Texture> textures;
    for (auto const& path : paths)
    {
        textures.push_back(createTexture(state, path, sampler));
    }

    return textures;
}

bool processEvent(Event const& event, App& app)
{
    std::cout << "Event\n";

    if (event.key == GLFW_KEY_W && event.action == GLFW_PRESS)
    {
        app.keyboard.up = true;
    }
    else if (event.key == GLFW_KEY_W && event.action == GLFW_RELEASE)
    {
        app.keyboard.up = false;
    }
    else if (event.key == GLFW_KEY_S && event.action == GLFW_PRESS)
    {
        app.keyboard.down = true;
    }
    else if (event.key == GLFW_KEY_S && event.action == GLFW_RELEASE)
    {
        app.keyboard.down = false;
    }
    else if (event.key == GLFW_KEY_A && event.action == GLFW_PRESS)
    {
        app.keyboard.left= true;
    }
    else if (event.key == GLFW_KEY_A && event.action == GLFW_RELEASE)
    {
        app.keyboard.left= false;
    }
    else if (event.key == GLFW_KEY_D && event.action == GLFW_PRESS)
    {
        app.keyboard.right = true;
    }
    else if (event.key == GLFW_KEY_D && event.action == GLFW_RELEASE)
    {
        app.keyboard.right = false;
    }
    else if (event.key == GLFW_KEY_ESCAPE && event.action == GLFW_PRESS)
    {
        return false;
    }

    return true;
}

void updateCamera(float delta, float camera_speed, vk::Extent2D const& extent, Camera& camera, App& app, GLFWwindow* window)
{
    if (app.keyboard.up)
    {
        camera.pos += camera_speed * camera.camera_front * delta;
    }
    if (app.keyboard.down)
    {
        camera.pos -= camera_speed * camera.camera_front * delta;
    }
    if (app.keyboard.right)
    {
        camera.pos += camera_speed * glm::normalize(glm::cross(camera.camera_front, camera.up)) * delta;
    }
    if (app.keyboard.left)
    {
        camera.pos -= camera_speed * glm::normalize(glm::cross(camera.camera_front, camera.up)) * delta;
    }

    if (app.cursor_pos.x != uint32_t(extent.width/2) || app.cursor_pos.y != uint32_t(extent.height/2))
    {
        glm::vec2 center = glm::vec2(extent.width/2, extent.height/2);
        glm::vec2 diff = glm::vec2(app.cursor_pos.x, app.cursor_pos.y) - center;
        diff.y = -diff.y;

        camera.pitch_yawn += diff * camera_speed * delta;

        if (camera.pitch_yawn.y >= 90)
        {
            camera.pitch_yawn.y = 89;
        }
        if (camera.pitch_yawn.y <= -90)
        {
            camera.pitch_yawn.y = -89;
        }

        glm::vec3 direction;
        direction.x = std::cos(glm::radians(camera.pitch_yawn.x)) * cos(glm::radians(camera.pitch_yawn.y));
        direction.y = std::sin(glm::radians(camera.pitch_yawn.y));
        direction.z = std::sin(glm::radians(camera.pitch_yawn.x)) * cos(glm::radians(camera.pitch_yawn.y));

        camera.camera_front = glm::normalize(direction);

        app.cursor_pos = CursorPos{uint32_t(extent.width/2), uint32_t(extent.height/2)};
        glfwSetCursorPos(window, app.cursor_pos.x, app.cursor_pos.y);
    }
}

int main()
{
    srand (time(NULL));
    RenderingState rendering_state = createVulkanRenderState();

    // Load textures
    std::cout << "Loading sampler\n";
    auto sampler = createTextureSampler(rendering_state);
    auto textures = loadTextures(rendering_state, sampler, {"textures/create.jpg", "textures/far.jpg", "textures/ridley.png", "textures/ground.jpg", "textures/stone.jpg"});

    auto render_system = createDefaultSystem(rendering_state, textures);
    auto grass = createGrass(rendering_state, textures);


    World world{grass, render_system, {}, {}};

    static auto start_time = std::chrono::high_resolution_clock::now();

    uint32_t fps = 0;
    float total_time = 0;
    bool first_frame = true;

    std::cout << "Starting rendering loop\n";

    float camera_speed = 20;

    rendering_state.app->cursor_pos = {rendering_state.swap_chain.extent.width/2, rendering_state.swap_chain.extent.height/2};
    glfwSetCursorPos(rendering_state.window, rendering_state.app->cursor_pos.x, rendering_state.app->cursor_pos.y);

    while (!glfwWindowShouldClose(rendering_state.window))
    {
        glfwPollEvents();

        auto& app = *rendering_state.app;
        while (rendering_state.app->events.size())
        {
            auto event = app.events.front();
            app.events.pop();

            if (!processEvent(event, app))
            {
                return 0;
            }
        }


        auto current_time = std::chrono::high_resolution_clock::now();
        float delta = std::chrono::duration<float, std::chrono::seconds::period>(current_time - start_time).count();
        total_time += delta;
        if (total_time > 1)
        {
            std::cout << "FPS: " << fps << '\n';
            fps = 0;
            total_time = 0;
        }
        else
        {
            fps += 1;
        }

        start_time = current_time;

        // Update camera based on basic wasd controller and mouse movement like in FPS
        if (!first_frame)
        {
            updateCamera(delta, camera_speed, rendering_state.swap_chain.extent, rendering_state.camera, app, rendering_state.window);
        }

        auto result = drawFrame(rendering_state, world);
        if (result == DrawResult::RESIZE)
        {
            recreateSwapchain(rendering_state);
        }
        else if (result == DrawResult::EXIT)
        {
            return 0;
        }

        first_frame = false;
        rendering_state.current_frame = (rendering_state.current_frame + 1) % 2;
    }
}
