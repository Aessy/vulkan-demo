#include "X11/Xlib.h"
#undef True
#undef False

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

#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "imgui.h"

#include "Gui.h"


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

        std::cout << glm::to_string(camera.pitch_yawn);

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

layer_types::Program createComputeFogProgram(vk::ImageView fog_view)
{
    layer_types::Program program_desc;
    program_desc.compute_shader = {("./shaders/fog.spv")};
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"binding image"}},
        .type = layer_types::BufferType::NoBuffer,
        .size = 1,
        .binding = layer_types::Binding {
            .name = {{"binding image"}},
            .binding = 0,
            .type = layer_types::BindingType::StorageImage,
            .size = 1,
            .compute = true,
            .storage_image = fog_view
        }
    }});
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"texture_buffer"}},
        .type = layer_types::BufferType::NoBuffer,
        .size = 1,
        .binding = layer_types::Binding {
            .name = {{"binding textures"}},
            .binding = 0,
            .type = layer_types::BindingType::TextureSampler,
            .size = 1,
            .compute = true
        }
    }});
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"test_program"}},
        .type = layer_types::BufferType::WorldBufferObject,
        .size = 1,
        .binding = layer_types::Binding {
            .name = {{"binding world"}},
            .binding = 0,
            .type = layer_types::BindingType::Uniform,
            .size = 1,
            .compute = true
        }
    }});

    return program_desc;
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
        .name = {{"terrain_buffer"}},
        .type = layer_types::BufferType::TerrainBufferObject,
        .size = 1,
        .binding = layer_types::Binding {
            .name = {{"binding model"}},
            .binding = 0,
            .type = layer_types::BindingType::Uniform,
            .size = 1,
            .vertex = true,
            .fragment = true,
            .tess_evu = true
        }
    }});

    return program_desc;
}

layer_types::Program createTerrainProgram()
{
    layer_types::Program program_desc;
    program_desc.fragment_shader = {{"./shaders/terrain_frag.spv"}};
    program_desc.vertex_shader= {{"./shaders/terrain_vert.spv"}};
    program_desc.tesselation_ctrl_shader = {{"./shaders/terrain_tess_ctrl.spv"}};
    program_desc.tesselation_evaluation_shader= {{"./shaders/terrain_tess_evu.spv"}};
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
            .tess_evu = true
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
            .tess_evu = true
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
        .name = {{"terrain_buffer"}},
        .type = layer_types::BufferType::TerrainBufferObject,
        .size = 1,
        .binding = layer_types::Binding {
            .name = {{"binding model"}},
            .binding = 0,
            .type = layer_types::BindingType::Uniform,
            .size = 1,
            .vertex = true,
            .fragment = true,
            .tess_evu = true
        }
    }});

    return program_desc;
}

int main()
{
    srand (time(NULL));
    RenderingState core = createVulkanRenderState();

    Textures textures = createTextures(core,
        { {"./textures/canyon2_height.png", TextureType::Map, vk::Format::eR8G8B8A8Unorm},
          {"./textures/canyon2_normals.png", TextureType::Map, vk::Format::eR8G8B8A8Unorm},
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
          {"./textures/dune_height.png", TextureType::Map, vk::Format::eR8G8B8A8Unorm},
          {"./textures/dune_normals.png", TextureType::Map, vk::Format::eR8G8B8A8Unorm},
          {"./textures/GroundSand005_COL_2K.jpg", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},
          {"./textures/GroundSand005_NRM_2K.jpg", TextureType::MipMap, vk::Format::eR8G8B8A8Unorm},
        });

    layer_types::Program program_desc;
    program_desc.fragment_shader = {{"./shaders/frag.spv"}};
    program_desc.vertex_shader= {{"./shaders/vert.spv"}};
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
            .fragment = true
        }
    }});
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"test_program"}},
        .type = layer_types::BufferType::WorldBufferObject,
        .size = 1,
        .binding = layer_types::Binding {
            .name = {{"binding world"}},
            .binding = 0,
            .type = layer_types::BindingType::Uniform,
            .size = 1,
            .vertex = true,
            .fragment = true
        }
    }});
    program_desc.buffers.push_back({layer_types::Buffer{
        .name = {{"test_program"}},
        .type = layer_types::BufferType::ModelBufferObject,
        .size = 500,
        .binding = layer_types::Binding {
            .name = {{"binding model"}},
            .binding = 0,
            .type = layer_types::BindingType::Storage,
            .size = 1,
            .vertex = true,
            .fragment = true
        }
    }});

    Models models;
    int plain_id = models.loadModel("./models/plain.obj");
    int cylinder_id = models.loadModel("./models/cylinder.obj");

    auto height_map_1_model = createFlatGround(511, 256, 4);
    models.models.insert({height_map_1_model.id, height_map_1_model});

    auto terrain_program_fill = createTerrainProgram();
    auto terrain_program_wireframe = terrain_program_fill;
    terrain_program_wireframe.polygon_mode = layer_types::PolygonMode::Line;

    auto terrain_program_triplanar = createTriplanarLandscapeProgram();

    std::vector<std::unique_ptr<Program>> programs;
    programs.push_back(createProgram(program_desc, core, textures, core.render_pass));
    programs.push_back(createProgram(terrain_program_fill, core, textures, core.render_pass));
    programs.push_back(createProgram(terrain_program_wireframe, core, textures, core.render_pass));
    programs.push_back(createProgram(terrain_program_triplanar, core, textures, core.render_pass));

    Camera camera;
    camera.proj = glm::perspective(glm::radians(45.0f), core.swap_chain.extent.width / (float)core.swap_chain.extent.height, 0.5f, 500.0f);
    camera.pitch_yawn = glm::vec2(-90, 0);
    camera.up = glm::vec3(0,1,0);
    camera.pos = glm::vec3(0,0,-5);

    updateCameraFront(camera);

    auto box = createBox();

    Meshes meshes;
    auto mesh_id = meshes.loadMesh(core, models.models.at(height_map_1_model.id), "height_map_1");
    auto cylinder_mesh_id = meshes.loadMesh(core, models.models.at(cylinder_id), "cylinder");

    //meshes.loadMesh(core, models.models.at(plain_id), "plain_ground");

    Scene scene;
    scene.camera = camera;
    scene.light.position = glm::vec3(12.8,56,-10);
    scene.light.light_color = glm::vec3(1,1,1);
    scene.light.strength = 50.0f;
    scene.objects[1].push_back(createObject(meshes.meshes.at(mesh_id)));

    for (int i = 0; i < programs.size(); ++i)
    {
        scene.materials[i] = {};
    }

    auto object = createObject(meshes.meshes.at(mesh_id));
    object.material = 1;

    auto cylinder = createObject(meshes.meshes.at(cylinder_mesh_id));
    cylinder.material = 0;
    addObject(scene, object);


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

    std::cout << "Starting rendering loop\n";

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
            updateCamera(delta, camera_speed, core.swap_chain.extent, application.scene.camera, app, core.window);
        }

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
