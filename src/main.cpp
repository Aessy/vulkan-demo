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

// Create a host-visible vertex + index buffer for line geometry.
static Buffer createLineVertexBuffer(RenderingState const& state, std::vector<LineVertex> const& verts)
{
    vk::DeviceSize size = sizeof(LineVertex) * verts.size();
    auto [buf, mem] = createBuffer(state, size,
        vk::BufferUsageFlagBits::eVertexBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    void* data = mem.mapMemory(0, size).value;
    memcpy(data, verts.data(), (size_t)size);
    mem.unmapMemory();
    return {std::move(buf), std::move(mem)};
}

// Unit circle (radius 1) in XZ plane, 128 LINE_LIST segments.
// color: rgba with alpha representing opacity.
static std::pair<std::vector<LineVertex>, std::vector<uint32_t>>
makeOrbitRingGeometry(glm::vec4 color, int N = 128)
{
    std::vector<LineVertex> verts(N);
    for (int i = 0; i < N; ++i)
    {
        float angle = (float)i / (float)N * 2.0f * glm::pi<float>();
        verts[i].pos   = glm::vec3(std::cos(angle), 0.0f, std::sin(angle));
        verts[i].color = color;
        verts[i].param = (float)i / (float)N;
    }
    std::vector<uint32_t> indices;
    indices.reserve(N * 2);
    for (int i = 0; i < N; ++i)
    {
        indices.push_back((uint32_t)i);
        indices.push_back((uint32_t)((i + 1) % N));
    }
    return {verts, indices};
}

// Recompute orbit ring vertex positions in double precision to avoid float32
// cancellation.  The ring is a circle of radius `radius_km` centred at the
// world origin.  We subtract cam.pos_d in double before converting to float,
// so the GPU only ever sees small CRR-space coordinates.
// N must match the vertex count used when the buffer was created.
static void updateOrbitRingVertices(Buffer& vbuf, double radius_km,
                                    glm::dvec3 const& cam_pos_d, int N = 128)
{
    vk::DeviceSize size = sizeof(LineVertex) * N;
    LineVertex* verts   = static_cast<LineVertex*>(vbuf.memory.mapMemory(0, size).value);
    double two_pi = 2.0 * glm::pi<double>();
    for (int i = 0; i < N; ++i)
    {
        double angle   = (double)i / (double)N * two_pi;
        glm::dvec3 world_km(std::cos(angle) * radius_km, 0.0, std::sin(angle) * radius_km);
        verts[i].pos   = glm::vec3(world_km - cam_pos_d);
    }
    vbuf.memory.unmapMemory();
}

// Grid in XZ plane: (2*half_n+1) lines in each direction, unit spacing.
// Scaled via model matrix.
static std::pair<std::vector<LineVertex>, std::vector<uint32_t>>
makeGridGeometry(glm::vec4 color, int half_n = 10)
{
    std::vector<LineVertex> verts;
    std::vector<uint32_t> indices;
    int total_lines = 2 * half_n + 1;
    verts.reserve(total_lines * 4);
    indices.reserve(total_lines * 4);

    auto add_line = [&](glm::vec3 a, glm::vec3 b)
    {
        uint32_t base = (uint32_t)verts.size();
        verts.push_back({a, color, 0.0f});
        verts.push_back({b, color, 1.0f});
        indices.push_back(base);
        indices.push_back(base + 1);
    };

    float half = (float)half_n;
    for (int i = -half_n; i <= half_n; ++i)
    {
        float f = (float)i;
        // Lines parallel to Z
        add_line(glm::vec3(f, 0.0f, -half), glm::vec3(f, 0.0f, half));
        // Lines parallel to X
        add_line(glm::vec3(-half, 0.0f, f), glm::vec3(half, 0.0f, f));
    }
    return {verts, indices};
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
        mat.emissive          = def.emissive;

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
    // Orbit camera: start above the ecliptic looking down at the solar system
    camera.orbit_target    = glm::dvec3(0.0);   // Sun
    camera.orbit_distance  = 3e8;               // 300M km — inner planets visible
    camera.orbit_azimuth   = 20.0f;
    camera.orbit_elevation = 30.0f;
    updateCameraFromOrbit(camera);

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

    // --- Line objects: orbit rings + ecliptic grid ---
    // All use program 3 (Lines pipeline).
    Material lines_material{.name = {"Lines"}, .program = 3, .shader_data = {}};

    // Orbit ring buffers — kept alive for the duration of the render loop.
    std::vector<Buffer> orbit_ring_vbufs;
    std::vector<Buffer> orbit_ring_ibufs;
    std::vector<int>    orbit_ring_obj_ids;
    // std::vector<double> orbit_ring_radii;  // semi_major_axis_km per ring, for per-frame CRR update

    for (int i = 0; i < (int)solar_system.defs.size(); ++i)
    {
        double r = solar_system.defs[i].semi_major_axis_km;
        if (r <= 0.0) continue;  // Sun has no orbit

        glm::vec3 col = solar_system.defs[i].albedo_color;
        auto [ring_verts, ring_indices] = makeOrbitRingGeometry(glm::vec4(col, 1.0f));

        auto vbuf = createLineVertexBuffer(core, ring_verts);
        auto ibuf = createIndexBuffer(core, ring_indices);

        // Positions are baked per-frame in double precision; model matrix is identity.
        // updateOrbitRingVertices(vbuf, r, camera.pos_d);

        Object obj{};
        obj.vertex_buffer = vbuf.buffer;
        obj.index_buffer  = ibuf.buffer;
        obj.indices_size  = (uint32_t)ring_indices.size();
        // obj.position      = glm::vec3(0.0f);   // baked into vertices
        obj.position      = glm::vec3(-camera.pos_d);
        obj.rotation      = glm::vec3(0.0f, 1.0f, 0.0f);
        obj.angel         = 0.0f;
        obj.scale         = (float)r;
        obj.material      = lines_material;
        obj.line_width    = solar_system.orbit_line_width;
        obj.line_alpha    = solar_system.orbit_opacity;
        obj.dash_count    = solar_system.orbit_stippled ? 20.0f : 0.0f;
        obj.visible       = solar_system.show_orbits;

        orbit_ring_obj_ids.push_back((int)scene.objs.size());
        //orbit_ring_radii.push_back(r);
        addObject(scene, obj);
        orbit_ring_vbufs.push_back(std::move(vbuf));
        orbit_ring_ibufs.push_back(std::move(ibuf));
    }

    // Grid mesh
    auto [grid_verts, grid_indices] = makeGridGeometry(
        glm::vec4(0.5f, 0.5f, 0.65f, 1.0f),
        solar_system.grid_line_count);
    auto grid_vbuf = createLineVertexBuffer(core, grid_verts);
    auto grid_ibuf = createIndexBuffer(core, grid_indices);

    int grid_obj_id = (int)scene.objs.size();
    {
        Object obj{};
        obj.vertex_buffer = grid_vbuf.buffer;
        obj.index_buffer  = grid_ibuf.buffer;
        obj.indices_size  = (uint32_t)grid_indices.size();
        obj.position      = glm::vec3(camera.orbit_target - camera.pos_d);
        obj.rotation      = glm::vec3(0.0f, 1.0f, 0.0f);
        obj.angel         = 0.0f;
        obj.scale         = solar_system.grid_spacing_km;
        obj.material      = lines_material;
        obj.line_width    = solar_system.grid_line_width;
        obj.line_alpha    = solar_system.grid_opacity;
        obj.dash_count    = 0.0f;
        obj.visible       = solar_system.show_grid;
        addObject(scene, obj);
    }

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

        // Update solar system orbital positions
        if (!first_frame && !solar_system.paused)
        {
            updateSolarSystem(solar_system, (double)delta);
        }

        // Update orbit_target from selected body BEFORE the camera update so that
        // updateCameraFromOrbit() inside updateOrbitCamera() uses the correct target.
        if (solar_system.selected_body < 0)
            application.scene.camera.orbit_target = solar_system.sun_position_km;
        else
            application.scene.camera.orbit_target =
                solar_system.states[solar_system.selected_body].position_km;

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Orbit camera: reads ImGui IO — must be after NewFrame().
        // Updates camera.pos_d and camera_front to this frame's values.
        updateOrbitCamera(application.scene.camera);

        // CRR positions: all use camera.pos_d which is now up-to-date.
        // View matrix (built in sceneWriteBuffers) uses camera_front from the same
        // updateOrbitCamera call, so positions and view are always consistent.

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

        // Update orbit ring objects: recompute CRR vertex positions in double
        // precision to eliminate float32 cancellation wobble.
        {
            int ring_idx = 0;
            for (int i = 0; i < (int)solar_system.defs.size(); ++i)
            {
                if (solar_system.defs[i].semi_major_axis_km <= 0.0) continue;
                auto& obj = application.scene.objs[orbit_ring_obj_ids[ring_idx++]];
                obj.position   = glm::vec3(-application.scene.camera.pos_d);
                obj.line_width = solar_system.orbit_line_width;
                obj.line_alpha = solar_system.orbit_opacity;
                obj.dash_count = solar_system.orbit_stippled ? 20.0f : 0.0f;
                obj.visible    = solar_system.show_orbits;
            }
        }

        // Update grid object: CRR offset to orbit_target + settings
        {
            auto& obj = application.scene.objs[grid_obj_id];
            obj.position   = glm::vec3(application.scene.camera.orbit_target
                                       - application.scene.camera.pos_d);
            obj.scale      = solar_system.grid_spacing_km;
            obj.line_width = solar_system.grid_line_width;
            obj.line_alpha = solar_system.grid_opacity;
            obj.visible    = solar_system.show_grid;
        }

        // Update sun position for lighting (sun is always at km origin)
        {
            glm::vec3 sun_cam_rel = glm::vec3(-application.scene.camera.pos_d);
            application.scene.light.position = sun_cam_rel;
            application.scene.light.sun_pos  = glm::length(sun_cam_rel) > 0.0f
                                               ? glm::normalize(sun_cam_rel)
                                               : glm::vec3(1.0f, 0.0f, 0.0f);
        }

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
