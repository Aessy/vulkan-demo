#include "Material.h"
#include "Skybox.h"
#include <X11/X.h>
#include <algorithm>
#include <vulkan/vulkan_raii.hpp>
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

#include "Pipelines/GeneralPurpuse.h"
#include "Pipelines/Skybox.h"

#include "RenderPass/ShadowMap.h"
#include "RenderPass/SceneRenderPass.h"

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

void recordCommandBuffer(RenderingState const& state, uint32_t image_index, Application& render_system)
{
    Application& app = render_system;

    // Write all buffer data used by the render passes.
    shadowPassWriteBuffers(state, render_system.scene, app.shadow_map, state.current_frame);
    sceneWriteBuffers(render_system.scene, state.current_frame);
    postProcessingWriteBuffers(app.ppp, state.current_frame);

    vk::raii::CommandBuffer const& command_buffer = state.command_buffer[state.current_frame];

    vk::CommandBufferBeginInfo begin_info{};
    begin_info.sType = vk::StructureType::eCommandBufferBeginInfo;
    begin_info.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    begin_info.pInheritanceInfo = nullptr;

    command_buffer.begin(begin_info);

    shadowMapRenderPass(state, app.shadow_map, app.scene, command_buffer);
    sceneRenderPass(command_buffer, state, render_system.scene_render_pass, render_system.scene, image_index);
    postProcessingRenderPass(state, app.ppp, command_buffer, render_system.scene, image_index);
    command_buffer.end();
}

enum class DrawResult
{
    SUCCESS,
    RESIZE,
    EXIT
};

template<typename RenderingSystem>
DrawResult drawFrame(RenderingState const& state, RenderingSystem& render_system)
{
    auto v = state.device.waitForFences({*state.semaphores.in_flight_fence[state.current_frame]}, true, ~0);

    ImGui::Render();
    /*
    vk::AcquireNextImageInfoKHR info{};
    info.timeout = ~0;
    info.setSemaphore(*state.semaphores.image_available_semaphore[state.current_frame]);
    info.setSwapchain(state.swap_chain.swap_chain);
    auto next_image = device.acquireNextImage2KHR(info);
    */

    vk::Device device = state.device;
    vk::SwapchainKHR swapchain = state.swap_chain.swap_chain;
    vk::Semaphore image_available_semaphore = *state.semaphores.image_available_semaphore[state.current_frame];
    vk::Semaphore render_finish_sempahore = *state.semaphores.render_finished_semaphore[state.current_frame];

    auto next_image = device.acquireNextImageKHR(swapchain, ~0, image_available_semaphore, VK_NULL_HANDLE);

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

    state.device.resetFences({*state.semaphores.in_flight_fence[state.current_frame]});

    state.command_buffer[state.current_frame].reset(static_cast<vk::CommandBufferResetFlags>(0));

    auto image_index = next_image.value;
    recordCommandBuffer(state, image_index, render_system);

    vk::PipelineStageFlags wait_stages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};

    vk::CommandBuffer cmd_buffer = state.command_buffer[state.current_frame];

    vk::SubmitInfo submit_info{};
    submit_info.sType = vk::StructureType::eSubmitInfo;
    submit_info.setWaitSemaphores(image_available_semaphore);
    submit_info.setWaitDstStageMask(wait_stages);
    submit_info.commandBufferCount = 1;
    submit_info.setCommandBuffers(cmd_buffer);
    submit_info.setSignalSemaphores(render_finish_sempahore);


    state.graphics_queue.submit(submit_info, {*state.semaphores.in_flight_fence[state.current_frame]});

    vk::SwapchainKHR swap_chain = state.swap_chain.swap_chain;

    vk::PresentInfoKHR present_info{};
    present_info.sType = vk::StructureType::ePresentInfoKHR;
    present_info.setWaitSemaphores(render_finish_sempahore);
    present_info.setResults(nullptr);
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swap_chain;
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

    static bool first_frame = true;
    static bool shift_was_up = true;
    if (shift_was_up && app.keyboard.shift)
    {
        first_frame = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);  // Hide and disable the cursor
        shift_was_up = false;
    }
    else if (!shift_was_up && !app.keyboard.shift)
    {
        first_frame = false;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);  // Hide and disable the cursor
        shift_was_up = true;
    }
    else if(app.keyboard.shift)
    {
        double xpos, ypos{};
        glfwGetCursorPos(window, &xpos, &ypos);

        if (first_frame)
        {
            app.cursor_pos = CursorPos{xpos, ypos};
            first_frame = false;
        }

        
        double xdiff = xpos - app.cursor_pos.x;
        double ydiff = ypos - app.cursor_pos.y;

        if (xdiff || ydiff)
        {
            app.cursor_pos = CursorPos{xpos, ypos};
        }
        

        glm::vec2 diff = glm::vec2(xdiff, ydiff);
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
    }
    else
    {
        shift_was_up = true;
    }
}

int main()
{
    srand (time(NULL));
    auto out = createVulkanRenderState();

    if (!out)
    {
        spdlog::warn("Could not create the vulkan render state. Exiting");
        return 0;
    }

    RenderingState& core = *out;

    spdlog::info("Loading textures");
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
          {"./textures/tree/Dead_Tree_qlEtl_High_4K_BaseColor.jpg", TextureType::MipMap, vk::Format::eR8G8B8A8Srgb},
          {"./textures/tree/Dead_Tree_qlEtl_High_4K_Normal.jpg", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},
          {"./textures/tree/Dead_Tree_qlEtl_High_4K_Roughness.jpg", TextureType::MipMap, vk::Format::eR8Unorm},
          {"./textures/tree/Dead_Tree_qlEtl_High_4K_AO.jpg", TextureType::MipMap, vk::Format::eR8Unorm},
        });

    spdlog::info("Loading models");
    Models models;
    // int landscape_fbx = models.loadModelAssimp("./models/canyon_low_res.fbx");
    int sphere_fbx = models.loadModelAssimp("./models/sky_sphere.fbx");
    // int dune_id = models.loadModelAssimp("./models/dune.fbx");
    
    int tree_fbx = models.loadModelAssimp("./textures/tree/Dead_Tree_qlEtl_High.fbx");

    // int cylinder_id = models.loadModel("./models/cylinder.obj");

    auto height_map_1_model = createFlatGround(512, 1024, 4);
    models.models.insert({height_map_1_model.id, height_map_1_model});
    //auto height_map_1_model = createFlatGround(2047, 500, 4);

    //models.models.insert({height_map_1_model.id, height_map_1_model});

    //auto terrain_program_wireframe = terrain_program_fill;
    //terrain_program_wireframe.polygon_mode = layer_types::PolygonMode::Line;

    Scene scene;
    scene.world_buffer = createUniformBuffers<WorldBufferObject>(core);
    scene.model_buffer = createStorageBuffers<ModelBufferObject>(core, 10);
    scene.material_buffer = createStorageBuffers<MaterialShaderData>(core, 10);
    scene.atmosphere_data = createUniformBuffers<Atmosphere>(core);

    auto shadow_map = createCascadedShadowMap(core, scene);
    auto scene_render_pass = createSceneRenderPass(core, textures, scene, shadow_map);

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
                                  MaterialFeatureFlag::AoMap
                                | MaterialFeatureFlag::AlbedoMap
                                | MaterialFeatureFlag::NormalMap,
            .sampling_mode = SamplingMode::TriplanarSampling,
            .shade_mode = ReflectionShadeMode::Pbr,
            .displacement_map_texture = 10,
            .normal_map_texture = 11,
            .base_color_texture = 12,
            .base_color_normal_texture = 13,
            .ao_texture = 14,
            .scaling_factor = 0.3f,
            .roughness = 0.402,
            .metallic = 0.922,
            .ao = 0,
        }
    };

    Material tree_material {
        .name = {"Tree"},
        .program = 0,
        .shader_data = {
            .material_features =  
                                  MaterialFeatureFlag::AoMap
                                | MaterialFeatureFlag::AlbedoMap
                                | MaterialFeatureFlag::NormalMap
                                | MaterialFeatureFlag::RoughnessMap,
            .sampling_mode = SamplingMode::UvSampling,
            .shade_mode = ReflectionShadeMode::Pbr,
            .base_color_texture = 21,
            .base_color_normal_texture = 22,
            .roughness_texture = 23,
            .ao_texture = 24,
            .scaling_factor = 1.0f,
            .roughness = 0.402,
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
            .shade_mode = ReflectionShadeMode::Phong,
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

    Material landscape_flat_dune{
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
            .displacement_y = 70.0f,
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
    camera.proj = glm::perspective(glm::radians(45.0f), core.swap_chain.extent.width / (float)core.swap_chain.extent.height, 0.5f, 500.0f);
    camera.pitch_yawn = glm::vec2(-90, 0);
    camera.up = glm::vec3(0,1,0);
    //camera.pos = glm::vec3(0,300,-5);
    camera.pos = glm::vec3(0,70, 0);

    updateCameraFront(camera);

    auto box = createBox();

    Meshes meshes;
    auto landscape_flat_id = meshes.loadMesh(core, models.models.at(height_map_1_model.id), "height_map_1");
    auto sphere_id = meshes.loadMesh(core, models.models.at(sphere_fbx), "sphere fbx");
    auto tree_id = meshes.loadMesh(core, models.models.at(tree_fbx), "sphere fbx");

    auto box_id = meshes.loadMesh(core, box, "box");

    scene.camera = camera;
    scene.light.position = glm::vec3(450000,0,0);
    scene.light.light_color = glm::vec3(1,1,1);
    scene.light.strength = 5.0f;

    glm::vec3 sun_sphere;
    sun_sphere.x = scene.atmosphere.sun_distance * glm::cos(0.0);
    sun_sphere.y = scene.atmosphere.sun_distance * glm::sin(0.0) * glm::sin(0.5f);
    sun_sphere.z = scene.atmosphere.sun_distance * glm::sin(0.0) * glm::cos(0.5f);
    scene.light.sun_pos = sun_sphere;


    for (int i = 0; i < scene_render_pass.pipelines.size(); ++i)
    {
        scene.programs[i] = {};
    }

    //auto object = createObject(meshes.meshes.at(mesh_id));
    //object.material = 1;

    auto landscape_flat = createObject(meshes.meshes.at(landscape_flat_id));
    landscape_flat.material = landscape_flat_dune;
    landscape_flat.shadow = true;
    // landscape_flat.shadow = true;

    auto sky_box = createObject(meshes.meshes.at(sphere_id));
    sky_box.material = sky_box_material;
    sky_box.scale = 500.0f;
    sky_box.object_type = ObjectType::SKYBOX;

    auto tree = createObject(meshes.meshes.at(tree_id));
    tree.material = tree_material;
    tree.position = glm::vec3(98.69,43.10,0);
    tree.shadow = true;
    tree.scale = 0.04;
    addObject(scene, tree);



    //auto dune_object = createObject(meshes.meshes.at(dune_mesh_id));
    //dune_object.material = dune_material;

    //addObject(scene, dune_object);
    addObject(scene, landscape_flat);
    addObject(scene, sky_box);

    auto fbx = createObject(meshes.meshes.at(sphere_id));
    fbx.material = base_material;
    fbx.position = glm::vec3(0,30,0);
    fbx.shadow = true;
    addObject(scene, fbx);

    fbx.position = glm::vec3(10, 69, 0);
    addObject(scene, fbx);

    fbx.position = glm::vec3(37, 54, 10);
    addObject(scene, fbx);

    auto box_fbx = createObject(meshes.meshes.at(box_id));
    box_fbx.material = base_material;
    box_fbx.position = glm::vec3(25,54,10);
    box_fbx.angel = 50.0f;
    box_fbx.shadow = true;
    addObject(scene, box_fbx);

    //addObject(scene, box_object);

    auto ppp = createPostProcessing(core, scene_render_pass, scene.world_buffer);
    Application application{
        .textures = std::move(textures),
        .models = std::move(models),
        .meshes = std::move(meshes),
        .programs = {},
        .scene = std::move(scene),
        .scene_render_pass = std::move(scene_render_pass),
        .ppp = std::move(ppp),
        .shadow_map = std::move(shadow_map)
    };

    initImgui(core.device, core.physical_device, core.instance, core.graphics_queue, application.ppp.render_pass, core, core.window, core.msaa);

    static auto start_time = std::chrono::high_resolution_clock::now();

    uint32_t fps = 0;
    float total_time = 0;
    bool first_frame = true;

    spdlog::info("Starting rendering loop");

    float camera_speed = 20;

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
            spdlog::info("Not supporting resize at the moment. Existing");
            return 0;
        }
        else if (result == DrawResult::EXIT)
        {
            return 0;
        }

        first_frame = false;
        core.current_frame = (core.current_frame + 1) % 2;
    }
}
