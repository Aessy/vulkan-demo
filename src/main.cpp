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
#include "Pipelines/Lines.h"

#include "RenderPass/ShadowMap.h"
#include "RenderPass/SceneRenderPass.h"

#include "SolarSystem.h"
#include "SolarSystemScene.h"

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

// Orbit camera: scroll to zoom, left drag to rotate, right drag to pan.
// Reads ImGui IO — call AFTER ImGui::NewFrame() each frame.
void updateOrbitCamera(Camera& camera)
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;  // ImGui window is active

    // Scroll = multiplicative zoom (each notch ±15%)
    if (io.MouseWheel != 0.0f)
    {
        camera.orbit_distance *= std::pow(0.85, (double)io.MouseWheel);
        camera.orbit_distance  = std::max(1.0, camera.orbit_distance);
    }

    // Left drag = orbit (rotate azimuth + elevation around target)
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
    {
        camera.orbit_azimuth   -= io.MouseDelta.x * 0.3f;
        camera.orbit_elevation += io.MouseDelta.y * 0.3f;
        if (camera.orbit_elevation >  89.0f) camera.orbit_elevation =  89.0f;
        if (camera.orbit_elevation < -89.0f) camera.orbit_elevation = -89.0f;
    }

    // Right drag = pan orbit target in the view plane
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f))
    {
        glm::vec3 right_v = glm::normalize(glm::cross(camera.camera_front, camera.up));
        double scale = camera.orbit_distance * 0.001;
        camera.orbit_target -= glm::dvec3(right_v) * (double)io.MouseDelta.x * scale;
        camera.orbit_target += glm::dvec3(camera.up) * (double)io.MouseDelta.y * scale;
    }

    updateCameraFromOrbit(camera);
}

int main()
{
    srand(time(nullptr));
    auto out = createVulkanRenderState();

    if (!out)
    {
        spdlog::warn("Could not create the vulkan render state. Exiting");
        return 0;
    }

    RenderingState& core = *out;

    spdlog::info("Loading textures");
    Textures textures = createTextures(core, {});

    spdlog::info("Loading models");
    Models models;

    auto planet_sphere = createUVSphere(1.0f, 64, 64);
    models.models.insert({planet_sphere.id, planet_sphere});

    // --- Scene buffers ---
    Scene scene;
    scene.world_buffer           = createUniformBuffers<WorldBufferObject>(core);
    scene.model_buffer           = createStorageBuffers<ModelBufferObject>(core, 20);
    scene.material_buffer        = createStorageBuffers<MaterialShaderData>(core, 20);
    scene.atmosphere_data        = createUniformBuffers<Atmosphere>(core);
    scene.planet_material_buffer = createStorageBuffers<PlanetMaterialData>(core, 20);

    auto shadow_map        = createCascadedShadowMap(core, scene);
    auto scene_render_pass = createSceneRenderPass(core, textures, scene, shadow_map);

    auto solar_system = createSolarSystem();

    // --- Camera ---
    Camera camera;
    camera.proj = glm::perspective(glm::radians(45.0f),
        core.swap_chain.extent.width / (float)core.swap_chain.extent.height,
        0.001f, 1e10f);
    camera.orbit_target    = glm::dvec3(0.0);
    camera.orbit_distance  = 3e8;
    camera.orbit_azimuth   = 20.0f;
    camera.orbit_elevation = 30.0f;
    updateCameraFromOrbit(camera);

    // --- Meshes ---
    Meshes meshes;
    auto planet_mesh_id = meshes.loadMesh(core, models.models.at(planet_sphere.id), "planet_sphere");

    scene.camera    = camera;
    scene.atmosphere = Atmosphere{};
    scene.light.light_color = glm::vec3(1.0f, 0.98f, 0.95f);
    scene.light.strength    = 5.0f;

    for (int i = 0; i < (int)scene_render_pass.pipelines.size(); ++i)
        scene.programs[i] = {};

    // --- Populate scene with planets and line objects ---
    initPlanetObjects(scene, solar_system, meshes.meshes.at(planet_mesh_id), camera);
    writePlanetMaterialBuffers(scene, solar_system, 0);
    writePlanetMaterialBuffers(scene, solar_system, 1);

    auto line_objects = initOrbitLines(core, scene, solar_system, camera);

    auto ppp = createPostProcessing(core, scene_render_pass, scene.world_buffer);
    Application application{
        .textures          = std::move(textures),
        .models            = std::move(models),
        .meshes            = std::move(meshes),
        .programs          = {},
        .scene             = std::move(scene),
        .scene_render_pass = std::move(scene_render_pass),
        .ppp               = std::move(ppp),
        .shadow_map        = std::move(shadow_map)
    };

    initImgui(core.device, core.physical_device, core.instance, core.graphics_queue,
              application.ppp.render_pass, core, core.window, core.msaa);

    static auto start_time = std::chrono::high_resolution_clock::now();

    uint32_t fps        = 0;
    float    total_time = 0.0f;
    bool     first_frame = true;

    spdlog::info("Starting rendering loop");

    while (!glfwWindowShouldClose(core.window))
    {
        glfwPollEvents();

        auto& app = *core.app;
        while (!app.events.empty())
        {
            auto event = app.events.front();
            app.events.pop();

            if (!processEvent(event, app))
                return 0;
        }

        auto current_time = std::chrono::high_resolution_clock::now();
        float delta = std::chrono::duration<float, std::chrono::seconds::period>(
                          current_time - start_time).count();
        total_time += delta;
        if (total_time > 1.0f)
        {
            spdlog::info("FPS: {}", fps);
            fps = 0;
            total_time = 0.0f;
        }
        else
        {
            ++fps;
        }
        start_time = current_time;

        // Advance simulation
        if (!first_frame && !solar_system.paused)
            updateSolarSystem(solar_system, static_cast<double>(delta));

        // Track selected body with the orbit camera — use the interpolated position
        // so the camera target and the rendered sphere always move in lock-step.
        if (solar_system.selected_body < 0)
            application.scene.camera.orbit_target = solar_system.sun_position_km;
        else
            application.scene.camera.orbit_target =
                interpolatedPosition(solar_system, solar_system.selected_body);

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Camera must be updated after NewFrame() so it can read ImGui IO.
        updateOrbitCamera(application.scene.camera);

        // Sync scene objects to current simulation state and camera position
        updateSceneFromSolarSystem(application.scene, solar_system, line_objects);
        updateSunLighting(application.scene, application.scene.camera);

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
