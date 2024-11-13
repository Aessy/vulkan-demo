#include "Material.h"
#include "Skybox.h"
#ifdef __linux__
#include "X11/Xlib.h"
#endif
#undef True
#undef False

#define VK_USE_PLATFORM_XLIB_KHR
#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"

#ifdef __linux__
#define GLFW_EXPOSE_NATIVE_X11
#elif _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif

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
#include "utilities.h"
#include "Scene.h"
#include "Program.h"
#include "Object.h"
#include "Textures.h"
#include "TypeLayer.h"
#include "Renderer.h"
#include "Application.h"
#include "PostProcessing.h"
#include "ShadowMap.h"

#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "imgui.h"

#include "Gui.h"

#include <spdlog/spdlog.h>


void loop(GLFWwindow* window)
{
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
    }
}


void draw(vk::CommandBuffer& command_buffer, Application& app, int frame)
{
    renderScene(command_buffer, app.scene, app.programs, frame);
    //draw(command_buffer, render_system.default_rendering, camera, frame);
    //draw(command_buffer, render_system.grass, camera, frame);
    //draw(command_buffer, render_system.terrain, camera, frame);
}

template<typename RenderingSystem>
void recordCommandBuffer(RenderingState& state, uint32_t image_index, RenderingSystem& render_system)
{
    // Bind the depth buffer to the fog program texture sampler
    Application& app = render_system;

    auto& command_buffer = state.command_buffer[state.current_frame];

    vk::CommandBufferBeginInfo begin_info{};
    begin_info.sType = vk::StructureType::eCommandBufferBeginInfo;
    begin_info.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    begin_info.pInheritanceInfo = nullptr;

    auto result = command_buffer.begin(begin_info);
    checkResult(result);

    //auto desc_set = state.descriptor_sets[0].set[0];
    //auto desc_set_2 = state.descriptor_sets[0].set[1];

/*
    runPipeline(command_buffer, render_system.scene, render_system.fog_program, state.current_frame);

    vk::MemoryBarrier memory_barrier{};
    memory_barrier.sType = vk::StructureType::eMemoryBarrier;
    memory_barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    memory_barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader, vk::DependencyFlags{}, memory_barrier, {}, {});
*/

    vk::RenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = vk::StructureType::eRenderPassBeginInfo;
    render_pass_info.setRenderPass(state.render_pass);
    render_pass_info.setFramebuffer(state.framebuffers[image_index]);
    render_pass_info.renderArea.offset = vk::Offset2D{0,0};
    render_pass_info.renderArea.extent = state.swap_chain.extent;

    std::array<vk::ClearValue, 2> clear_values{};
    clear_values[0].color = vk::ClearColorValue(std::array<float,4>{0.3984,0.695,1});
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

    command_buffer.beginRenderPass(&render_pass_info,
                                   vk::SubpassContents::eInline);
    draw(command_buffer, render_system, state.current_frame);

    command_buffer.endRenderPass();

    postProcessingRenderPass(state, app.ppp, command_buffer, render_system.scene, image_index);
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

    ImGui::Render();

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
        spdlog::warn("Failed to acquire swap chain image");
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


bool processEvent(Event const& event, App& app)
{
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
    else if (event.key == GLFW_KEY_LEFT_SHIFT && event.action == GLFW_PRESS)
    {
        app.keyboard.shift = true;
    }
    else if (event.key == GLFW_KEY_LEFT_SHIFT && event.action == GLFW_RELEASE)
    {
        app.keyboard.shift = false;
    }

    return true;
}

void updateCameraFront(Camera& camera)
{
    glm::vec3 direction;
    direction.x = std::cos(glm::radians(camera.pitch_yawn.x)) * cos(glm::radians(camera.pitch_yawn.y));
    direction.y = std::sin(glm::radians(camera.pitch_yawn.y));
    direction.z = std::sin(glm::radians(camera.pitch_yawn.x)) * cos(glm::radians(camera.pitch_yawn.y));

    camera.camera_front = glm::normalize(direction);
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

    static bool shift_was_up = true;
    if (shift_was_up && app.keyboard.shift)
    {
        app.cursor_pos = CursorPos{uint32_t(extent.width/2), uint32_t(extent.height/2)};
        glfwSetCursorPos(window, app.cursor_pos.x, app.cursor_pos.y);
        shift_was_up = false;
    }
    else if(app.keyboard.shift && (app.cursor_pos.x != uint32_t(extent.width/2) || app.cursor_pos.y != uint32_t(extent.height/2)))
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

        updateCameraFront(camera);

        app.cursor_pos = CursorPos{uint32_t(extent.width/2), uint32_t(extent.height/2)};
        glfwSetCursorPos(window, app.cursor_pos.x, app.cursor_pos.y);
    }
    else
    {
        shift_was_up = true;
    }
}

static layer_types::Program createTriplanarLandscapeProgram()
{
    layer_types::Program program_desc;
    program_desc.fragment_shader = {{"./shaders/triplanar_frag.spv"}};
    program_desc.vertex_shader= {{"./shaders/triplanar_vert.spv"}};
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"texture_buffer"}},
        .type = layer_types::BufferType::NoBuffer,
        .size = 1,
        .binding = layer_types::Binding {
            .name = {{"binding textures"}},
            .binding = 0,
            .type = layer_types::BindingType::TextureSampler,
            .size = 32,
            .vertex = true,
            .fragment = true,
        }
    }});
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"world_buffer"}},
        .type = layer_types::BufferType::WorldBufferObject,
        .size = 1,
        .binding = layer_types::Binding {
            .name = {{"binding world"}},
            .binding = 0,
            .type = layer_types::BindingType::Uniform,
            .size = 1,
            .vertex = true,
            .fragment = true,
            .tess_ctrl = true,
        }
    }});
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"model_buffer"}},
        .type = layer_types::BufferType::ModelBufferObject,
        .size = 10,
        .binding = layer_types::Binding {
            .name = {{"binding model"}},
            .binding = 0,
            .type = layer_types::BindingType::Storage,
            .size = 1,
            .vertex = true,
            .fragment = true
        }
    }});

    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"material shader data"}},
        .type = layer_types::BufferType::MaterialShaderData,
        .size = 10,
        .binding = layer_types::Binding {
            .name = {{"binding model"}},
            .binding = 0,
            .type = layer_types::BindingType::Storage,
            .size = 1,
            .vertex = true,
            .fragment = true,
        }
    }});

    return program_desc;
}

int main()
{
    srand (time(NULL));
    RenderingState core = createVulkanRenderState();

    Textures textures = createTextures(core,
        { 
          //{"./textures/canyon2_height.png", TextureType::Map, vk::Format::eR8G8B8A8Unorm},
          //{"./textures/canyon2_normals.png", TextureType::Map, vk::Format::eR8G8B8A8Unorm},
          {"./textures/terrain.png", TextureType::Map, vk::Format::eR8G8B8A8Unorm},
          {"./textures/terrain_norm_high.png", TextureType::Map, vk::Format::eR8G8B8A8Unorm},
          {"./textures/terrain_norm_low.png", TextureType::Map, vk::Format::eR8G8B8A8Unorm},
          {"./textures/terrain_norm_flat.png", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},
          {"./textures/stone.jpg", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},
          {"./textures/checkerboard.jpg", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},
          {"./textures/forest_normal.png", TextureType::Map, vk::Format::eR8G8B8A8Unorm},
          {"./textures/forest_normal.png", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},
          {"./textures/forest_diff.png", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},
          {"./textures/forest_diff.png", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},
          {"./textures/dune3_height.png", TextureType::Map, vk::Format::eR8G8B8A8Unorm},
          {"./textures/dune3_normals.png", TextureType::Map, vk::Format::eR8G8B8A8Unorm},
          //{"./textures/cylinder.png", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},
          {"./textures/GroundSand005_COL_2K.jpg", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},
          {"./textures/GroundSand005_NRM_2K.jpg", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},
          {"./textures/GroundSand005_AO_2K.jpg", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},
          {"./textures/brown_mud_03_diff_1k.jpg", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},
          {"./textures/brown_mud_03_nor_gl_1k.jpg", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},

          //{"./textures/terrain/canyon/canyon_height.png", TextureType::Map, vk::Format::eR8G8B8A8Unorm},
          //{"./textures/terrain/canyon/canyon_normals.png", TextureType::Map, vk::Format::eR8G8B8A8Unorm},

          //{"./textures/canyon/Canyon_Mud_Wall_wdtbbji_4K_BaseColor.jpg", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},
          //{"./textures/canyon/Canyon_Mud_Wall_wdtbbji_4K_Normal.jpg", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},

          //{"./textures/canyon/Canyon_Sandstone_Rock_vimldgeg_4K_BaseColor.jpg", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},
          //{"./textures/canyon/Canyon_Sandstone_Rock_vimldgeg_4K_Normal.jpg", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},
          //{"./textures/canyon/Canyon_Sandstone_Rock_vimldgeg_4K_Roughness.jpg", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},
          //{"./textures/canyon/Canyon_Sandstone_Rock_vimldgeg_4K_AO.jpg", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},

          {"./textures/Rippled_Sand_Dune_vd3mbbus_4K_BaseColor.jpg", TextureType::MipMap, vk::Format::eR8G8B8A8Srgb},
          {"./textures/Rippled_Sand_Dune_vd3mbbus_4K_Normal.jpg", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},
          {"./textures/Rippled_Sand_Dune_vd3mbbus_4K_Roughness.jpg", TextureType::MipMap, vk::Format::eR8Unorm},
          {"./textures/Rippled_Sand_Dune_vd3mbbus_4K_AO.jpg", TextureType::MipMap, vk::Format::eR8Unorm},
        });

    Models models;
    // int landscape_fbx = models.loadModelAssimp("./models/canyon_low_res.fbx");
    int sphere_fbx = models.loadModelAssimp("./models/sky_sphere.fbx");
    // int dune_id = models.loadModelAssimp("./models/dune.fbx");
    // int cylinder_id = models.loadModel("./models/cylinder.obj");

    auto height_map_1_model = createFlatGround(2047, 1024, 4);
    models.models.insert({height_map_1_model.id, height_map_1_model});
    //auto height_map_1_model = createFlatGround(2047, 500, 4);

    //models.models.insert({height_map_1_model.id, height_map_1_model});

    //auto terrain_program_wireframe = terrain_program_fill;
    //terrain_program_wireframe.polygon_mode = layer_types::PolygonMode::Line;

    auto terrain_program_triplanar = createTriplanarLandscapeProgram();

    auto pipeline_input = createDefaultPipelineInput();

    std::vector<std::unique_ptr<Program>> programs;
    programs.push_back(createProgram(terrain_program_triplanar, core, textures, core.render_pass, "Landscape triplanar", pipeline_input));

    auto skybox = createSkybox(core, core.render_pass,{
        "./textures/grass.png",
        "./textures/grass.png",
        "./textures/grass.png",
        "./textures/grass.png",
        "./textures/grass.png",
        "./textures/grass.png",
    });

    programs.push_back(std::move(skybox.program));

    Material sky_box_material {
        .name = {"Skybox"},
        .program = 1,
        .shader_data = {}
    };

    Material base_material{
        .name = {"Base"},
        .program = 0,
        .shader_data = {
            .material_features =  
                                  MaterialFeatureFlag::RoughnessMap
                                | MaterialFeatureFlag::AoMap
                                | MaterialFeatureFlag::AlbedoMap
                                | MaterialFeatureFlag::NormalMap,
            .sampling_mode = SamplingMode::TriplanarSampling,
            .shade_mode = ReflectionShadeMode::Pbr,
            .displacement_map_texture = 10,
            .normal_map_texture = 11,
            .base_color_texture = 17,
            .base_color_normal_texture = 18,
            .roughness_texture = 19,
            .ao_texture = 20,
            .scaling_factor = 10.0f,
            .roughness = 0,
            .metallic = 0,
            .ao = 0,
        }
    };

    Material landscape_material{
        .name = {"Landscape"},
        .program = 0,
        .shader_data = {
            .material_features =  MaterialFeatureFlag::DisplacementMap
                                | MaterialFeatureFlag::DisplacementNormalMap
                                | MaterialFeatureFlag::RoughnessMap
                                | MaterialFeatureFlag::AoMap
                                | MaterialFeatureFlag::AlbedoMap
                                | MaterialFeatureFlag::NormalMap,
            .sampling_mode = SamplingMode::TriplanarSampling,
            .shade_mode = ReflectionShadeMode::Pbr,
            .displacement_map_texture = 10,
            .normal_map_texture = 11,
            .displacement_y = 64.0f,
            .base_color_texture = 17,
            .base_color_normal_texture = 18,
            .roughness_texture = 19,
            .ao_texture = 20,
            .scaling_factor = 0.1f,
            .roughness = 0,
            .metallic = 0,
            .ao = 0,
        }
    };

    Material dune_material{
        .name = {"Dune"},
        .program = 0,
        .shader_data = {
            .material_features =  
                                  MaterialFeatureFlag::RoughnessMap
                                | MaterialFeatureFlag::AoMap
                                | MaterialFeatureFlag::AlbedoMap
                                | MaterialFeatureFlag::NormalMap,
            .sampling_mode = SamplingMode::TriplanarSampling,
            .shade_mode = ReflectionShadeMode::Pbr,
            .base_color_texture = 17,
            .base_color_normal_texture = 18,
            .roughness_texture = 19,
            .ao_texture = 20,
            .scaling_factor = 0.1f,
            .roughness = 0,
            .metallic = 0,
            .ao = 0,
        }
    };

    Camera camera;
    camera.proj = glm::perspective(glm::radians(45.0f), core.swap_chain.extent.width / (float)core.swap_chain.extent.height, 0.5f, 100000.0f);
    camera.pitch_yawn = glm::vec2(-90, 0);
    camera.up = glm::vec3(0,1,0);
    //camera.pos = glm::vec3(0,300,-5);
    camera.pos = glm::vec3(0,60,0);

    updateCameraFront(camera);

    auto box = createBox();

    Meshes meshes;
    auto landscape_flat_id = meshes.loadMesh(core, models.models.at(height_map_1_model.id), "height_map_1");
    // auto cylinder_mesh_id = meshes.loadMesh(core, models.models.at(cylinder_id), "cylinder");
    //auto landscape_mesh_id = meshes.loadMesh(core, models.models.at(landscape_fbx), "landscape fbx");
    auto sphere_id = meshes.loadMesh(core, models.models.at(sphere_fbx), "sphere fbx");
    //auto dune_mesh_id = meshes.loadMesh(core, models.models.at(dune_id), "Dune");

    //meshes.loadMesh(core, models.models.at(plain_id), "plain_ground");

    auto box_id = meshes.loadMesh(core, box, "box");

    Scene scene;
    scene.camera = camera;
    scene.light.position = glm::vec3(450000,0,0);
    scene.light.light_color = glm::vec3(1,1,1);
    scene.light.strength = 5.0f;
    
    // Sun light comes from directly down.
    //scene.objects[1].push_back(createObject(meshes.meshes.at(mesh_id)));

    for (int i = 0; i < programs.size(); ++i)
    {
        scene.programs[i] = {};
    }

    //auto object = createObject(meshes.meshes.at(mesh_id));
    //object.material = 1;

    auto landscape_flat = createObject(meshes.meshes.at(landscape_flat_id));
    landscape_flat.material = landscape_material;

    auto sky_box = createObject(meshes.meshes.at(sphere_id));
    sky_box.material = sky_box_material;
    sky_box.scale = 800.0f;

    auto fbx = createObject(meshes.meshes.at(sphere_id));
    fbx.material = base_material;
    fbx.position = glm::vec3(0,200,0);


    //auto dune_object = createObject(meshes.meshes.at(dune_mesh_id));
    //dune_object.material = dune_material;

    //addObject(scene, dune_object);
    addObject(scene, landscape_flat);
    addObject(scene, sky_box);
    addObject(scene, fbx);
    //addObject(scene, box_object);

    createCascadedShadowMap(core);

    Application application{
        .textures = std::move(textures),
        .models = std::move(models),
        .meshes = std::move(meshes),
        .programs = std::move(programs),
        .scene = std::move(scene),
        .ppp = createPostProcessing(core)
    };

    initImgui(core.device, core.physical_device, core.instance, core.graphics_queue, application.ppp.render_pass, core, core.window, core.msaa);

    static auto start_time = std::chrono::high_resolution_clock::now();

    uint32_t fps = 0;
    float total_time = 0;
    bool first_frame = true;

    spdlog::info("Starting rendering loop");

    float camera_speed = 20;

    core.app->cursor_pos = {core.swap_chain.extent.width/2, core.swap_chain.extent.height/2};
    glfwSetCursorPos(core.window, core.app->cursor_pos.x, core.app->cursor_pos.y);


    while (!glfwWindowShouldClose(core.window))
    {
        glfwPollEvents();

        auto& app = *core.app;
        while (core.app->events.size())
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
            spdlog::info("Time of the day: {}", application.scene.light.time_of_the_day);
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
            updateCamera(delta, camera_speed, core.swap_chain.extent, application.scene.camera, app, core.window);
        }

/*
        float theta = application.scene.light.sun_vertical_angle;
        float phi = application.scene.light.sun_horizontal_angle;

        glm::vec3 sun_sphere;
        sun_sphere.x = application.scene.atmosphere.sun_distance * glm::cos(phi);
        sun_sphere.y = application.scene.atmosphere.sun_distance * glm::sin(phi) * glm::sin(theta);
        sun_sphere.z = application.scene.atmosphere.sun_distance * glm::sin(phi) * glm::sin(theta);
        application.scene.atmosphere.sun_pos = sun_sphere;
*/
        //float sun_angle = application.scene.light.time_of_the_day * glm::pi<float>();
        //application.scene.light.sun_dir = glm::normalize(glm::vec3(glm::cos(sun_angle), glm::sin(sun_angle), 0.0f));

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        ImGui::NewFrame();

        // ImGui::ShowDemoWindow();

        gui::createGui(core, application);

        auto result = drawFrame(core, application);
        if (result == DrawResult::RESIZE)
        {
            recreateSwapchain(core);
        }
        else if (result == DrawResult::EXIT)
        {
            return 0;
        }

        first_frame = false;
        core.current_frame = (core.current_frame + 1) % 2;
    }
}
