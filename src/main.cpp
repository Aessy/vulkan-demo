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
#include "Pipelines/Planet.h"

#include "RenderPass/ShadowMap.h"
#include "RenderPass/SceneRenderPass.h"

#include "SolarSystem.h"

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

// Camera-relative rendering: movement accumulates into pos_d (double),
// camera.pos stays at (0,0,0) so the view matrix is always near-origin.
void updateCamera(float delta, float camera_speed, vk::Extent2D const& extent, Camera& camera, App& app, GLFWwindow* window)
{
    glm::dvec3 disp{0.0};
    glm::dvec3 front = glm::dvec3(camera.camera_front);
    glm::dvec3 right = glm::dvec3(glm::normalize(glm::cross(camera.camera_front, camera.up)));

    if (app.keyboard.up)
        disp += front * (double)camera_speed * (double)delta;
    if (app.keyboard.down)
        disp -= front * (double)camera_speed * (double)delta;
    if (app.keyboard.right)
        disp += right * (double)camera_speed * (double)delta;
    if (app.keyboard.left)
        disp -= right * (double)camera_speed * (double)delta;

    camera.pos_d += disp;
    camera.pos = glm::vec3(0.0f);  // always at origin for CRR

    static bool first_frame = true;
    static bool shift_was_up = true;
    if (shift_was_up && app.keyboard.shift)
    {
        first_frame = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        shift_was_up = false;
    }
    else if (!shift_was_up && !app.keyboard.shift)
    {
        first_frame = false;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
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

        const float MOUSE_SENSITIVITY = 0.1f;  // degrees per pixel
        camera.pitch_yawn += diff * MOUSE_SENSITIVITY;

        if (camera.pitch_yawn.y >= 90)
            camera.pitch_yawn.y = 89;
        if (camera.pitch_yawn.y <= -90)
            camera.pitch_yawn.y = -89;

        updateCameraFront(camera);
    }
    else
    {
        shift_was_up = true;
    }
}

// Write PlanetMaterialData to the planet_material_buffer at the correct global draw indices.
// Global draw index: programs[0].size() + programs[1].size() + i_in_program2
static void writePlanetMaterialBuffers(Scene& scene, SolarSystem const& ss, int frame)
{
    // Count objects in programs 0 and 1 to get starting index for program 2
    int base_index = 0;
    for (auto const& [prog, obj_list] : scene.programs)
    {
        if (prog >= 2) break;
        base_index += (int)obj_list.size();
    }

    int prog2_count = (int)scene.programs[2].size();
    for (int i = 0; i < prog2_count && i < (int)ss.defs.size(); ++i)
    {
        auto const& def = ss.defs[i];

        PlanetMaterialData mat;
        mat.diffuse_texture   = def.diffuse_texture_index;
        mat.normal_texture    = def.normal_texture_index;
        mat.has_normal_map    = (def.normal_texture_index >= 0) ? 1 : 0;
        mat.has_atmosphere    = def.has_atmosphere ? 1 : 0;
        mat.atmosphere_color_scale = glm::vec4(def.atmosphere_color, def.atmosphere_scale);
        mat.albedo_color      = glm::vec4(def.albedo_color, 1.0f);
        mat.roughness         = def.roughness;
        mat.metallic          = def.metallic;

        writeBuffer(*scene.planet_material_buffer[frame], mat, base_index + i);
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

    // Minimal texture set — just enough for existing infrastructure.
    // Planet textures would go here (NASA maps). For now, -1 (no texture) = albedo_color fallback.
    spdlog::info("Loading textures");
    Textures textures = createTextures(core, {});

    spdlog::info("Loading models");
    Models models;

    // Create UV sphere for planets (64 stacks × 64 slices)
    auto planet_sphere = createUVSphere(1.0f, 64, 64);
    models.models.insert({planet_sphere.id, planet_sphere});

    // --- Scene buffers ---
    Scene scene;
    scene.world_buffer           = createUniformBuffers<WorldBufferObject>(core);
    scene.model_buffer           = createStorageBuffers<ModelBufferObject>(core, 20);
    scene.material_buffer        = createStorageBuffers<MaterialShaderData>(core, 20);
    scene.atmosphere_data        = createUniformBuffers<Atmosphere>(core);
    scene.planet_material_buffer = createStorageBuffers<PlanetMaterialData>(core, 20);

    auto shadow_map       = createCascadedShadowMap(core, scene);
    auto scene_render_pass = createSceneRenderPass(core, textures, scene, shadow_map);

    // --- Solar system (needed early for initial camera position) ---
    auto solar_system = createSolarSystem();

    // --- Camera ---
    Camera camera;
    // Near = 0.001 km, Far = 1e10 km (logarithmic depth handles precision)
    camera.proj = glm::perspective(glm::radians(45.0f),
        core.swap_chain.extent.width / (float)core.swap_chain.extent.height,
        0.001f, 1e10f);
    // pitch=-90 → camera_front=(0,0,-1), pointing in -Z
    camera.pitch_yawn = glm::vec2(-90.0f, 0.0f);
    camera.up  = glm::vec3(0.0f, 1.0f, 0.0f);
    camera.pos = glm::vec3(0.0f);
    // Start 1,000,000 km above Earth in +Z, looking at it (-Z direction)
    camera.pos_d = solar_system.states[3].position_km + glm::dvec3(0.0, 0.0, 1000000.0);
    updateCameraFront(camera);

    // --- Meshes ---
    Meshes meshes;
    auto planet_mesh_id  = meshes.loadMesh(core, models.models.at(planet_sphere.id), "planet_sphere");

    scene.camera = camera;

    // Sun direction: camera-relative (sun at origin km, camera at pos_d)
    glm::vec3 sun_dir = glm::normalize(glm::vec3(-camera.pos_d));
    scene.light.position    = glm::vec3(-camera.pos_d);
    scene.light.light_color = glm::vec3(1.0f, 0.98f, 0.95f);
    scene.light.strength    = 5.0f;
    scene.light.sun_pos     = sun_dir;

    // Skybox atmosphere sun position (legacy field)
    scene.atmosphere = Atmosphere{};

    for (int i = 0; i < (int)scene_render_pass.pipelines.size(); ++i)
        scene.programs[i] = {};

    // --- Solar system (already created above) ---
    // Add each body as a planet object (program 2)
    Material planet_material{
        .name = {"Planet"},
        .program = 2,
        .shader_data = {}  // not used by planet shader; planet_material_buffer is used instead
    };

    for (int i = 0; i < (int)solar_system.defs.size(); ++i)
    {
        auto& def   = solar_system.defs[i];
        auto& state = solar_system.states[i];

        glm::vec3 cam_rel_pos = glm::vec3(state.position_km - camera.pos_d);

        auto obj = createObject(meshes.meshes.at(planet_mesh_id));
        obj.material  = planet_material;
        obj.position  = cam_rel_pos;
        obj.scale     = (float)def.radius_km;
        obj.rotation  = glm::vec3(0.0f, 1.0f, 0.0f);
        obj.angel     = 0.0f;

        state.scene_object_index = (int)scene.objs.size();
        addObject(scene, obj);
    }

    // Write planet material data into planet_material_buffer for both frame slots
    writePlanetMaterialBuffers(scene, solar_system, 0);
    writePlanetMaterialBuffers(scene, solar_system, 1);

    auto ppp = createPostProcessing(core, scene_render_pass, scene.world_buffer);
    Application application{
        .textures      = std::move(textures),
        .models        = std::move(models),
        .meshes        = std::move(meshes),
        .programs      = {},
        .scene         = std::move(scene),
        .scene_render_pass = std::move(scene_render_pass),
        .ppp           = std::move(ppp),
        .shadow_map    = std::move(shadow_map)
    };

    initImgui(core.device, core.physical_device, core.instance, core.graphics_queue,
              application.ppp.render_pass, core, core.window, core.msaa);

    static auto start_time = std::chrono::high_resolution_clock::now();

    uint32_t fps = 0;
    float total_time = 0.0f;
    bool first_frame = true;

    spdlog::info("Starting rendering loop");

    while (!glfwWindowShouldClose(core.window))
    {
        glfwPollEvents();

        auto& app = *core.app;
        while (core.app->events.size())
        {
            auto event = app.events.front();
            app.events.pop();

            if (!processEvent(event, app))
                return 0;
        }

        auto current_time = std::chrono::high_resolution_clock::now();
        float delta = std::chrono::duration<float, std::chrono::seconds::period>(current_time - start_time).count();
        total_time += delta;
        if (total_time > 1.0f)
        {
            spdlog::info("FPS: {}", fps);
            fps = 0;
            total_time = 0.0f;
        }
        else
        {
            fps += 1;
        }

        start_time = current_time;

        // Update camera (CRR)
        if (!first_frame)
        {
            // Adaptive speed: slow near surfaces, fast in open space
            double nearest_dist = 1e30;
            for (auto const& s : solar_system.states)
                nearest_dist = std::min(nearest_dist, (double)glm::length(s.position_km - application.scene.camera.pos_d));

            const float BASE_SPEED_KM_S = 1.0f; // 1 km/s base
            float camera_speed = BASE_SPEED_KM_S * (float)std::max(1.0, nearest_dist / 100.0);

            updateCamera(delta, camera_speed, core.swap_chain.extent,
                         application.scene.camera, app, core.window);
        }

        // Update solar system orbital positions
        if (!first_frame && !solar_system.paused)
        {
            updateSolarSystem(solar_system, (double)delta);
        }

        // Update camera-relative planet positions and rotation
        for (int i = 0; i < (int)solar_system.states.size(); ++i)
        {
            auto& state = solar_system.states[i];
            if (state.scene_object_index >= 0)
            {
                auto& obj    = application.scene.objs[state.scene_object_index];
                obj.position = glm::vec3(state.position_km - application.scene.camera.pos_d);
                obj.angel    = (float)glm::degrees(state.rotation_angle);
            }
        }

        // Update sun position for lighting (sun is always at km origin)
        {
            glm::vec3 sun_cam_rel = glm::vec3(-application.scene.camera.pos_d);
            application.scene.light.position = sun_cam_rel;
            application.scene.light.sun_pos  = glm::length(sun_cam_rel) > 0.0f
                                               ? glm::normalize(sun_cam_rel)
                                               : glm::vec3(1.0f, 0.0f, 0.0f);
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        gui::createGui(core, application, &solar_system);

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
