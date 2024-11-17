project(vulkan)

cmake_minimum_required(VERSION 3.21)

set(CMAKE_C_COMPILER gcc.exe)
set(CMAKE_C_COMPILER g++.exe)

set(SRC src/VulkanRenderSystem.cpp
        src/main.cpp
        src/PostProcessing.cpp
        src/height_map.cpp
        src/Gui.cpp
        src/Model.cpp
        src/Program.cpp
        src/Textures.cpp
        src/Id.cpp
        src/Skybox.cpp
        src/ShadowMap.cpp
        src/RenderPass/SceneRenderPass.cpp
        src/Pipelines/GeneralPurpuse.cpp
        src/Pipelines/Skybox.cpp
        src/imgui_impl_vulkan.cpp
        src/imgui_impl_glfw.cpp
        )

add_executable(vulkan-test ${SRC})
target_compile_options(vulkan-test PUBLIC -g -std=c++23)
target_compile_definitions(vulkan-test PUBLIC GLM_ENABLE_EXPERIMENTAL)
target_include_directories(vulkan-test PUBLIC src)

IF(WIN32)
target_link_libraries(vulkan-test vulkan-1 glfw3 libassimp)
ELSEIF(LINUX)
target_link_libraries(vulkan-test glfw vulkan X11 imgui assimp fmt spdlog)
ENDIF()

add_custom_target(shaders
                  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
                  COMMAND glslc ${CMAKE_SOURCE_DIR}/shaders/shader.frag --target-spv=spv1.5 -o ${CMAKE_BINARY_DIR}/shaders/frag.spv
                  COMMAND glslc ${CMAKE_SOURCE_DIR}/shaders/shader.vert --target-spv=spv1.5 -o ${CMAKE_BINARY_DIR}/shaders/vert.spv
                  COMMAND glslc ${CMAKE_SOURCE_DIR}/shaders/grass.frag --target-spv=spv1.5 -o ${CMAKE_BINARY_DIR}/shaders/grass_frag.spv
                  COMMAND glslc ${CMAKE_SOURCE_DIR}/shaders/grass.vert --target-spv=spv1.5 -o ${CMAKE_BINARY_DIR}/shaders/grass_vert.spv
                  COMMAND glslc ${CMAKE_SOURCE_DIR}/shaders/terrain.frag --target-spv=spv1.5 -o ${CMAKE_BINARY_DIR}/shaders/terrain_frag.spv
                  COMMAND glslc ${CMAKE_SOURCE_DIR}/shaders/terrain.vert --target-spv=spv1.5 -o ${CMAKE_BINARY_DIR}/shaders/terrain_vert.spv
                  COMMAND glslc ${CMAKE_SOURCE_DIR}/shaders/terrain.tesc --target-spv=spv1.5 -o ${CMAKE_BINARY_DIR}/shaders/terrain_tess_ctrl.spv
                  COMMAND glslc ${CMAKE_SOURCE_DIR}/shaders/terrain.tese --target-spv=spv1.5 -o ${CMAKE_BINARY_DIR}/shaders/terrain_tess_evu.spv
                  COMMAND glslc ${CMAKE_SOURCE_DIR}/shaders/fog.comp --target-spv=spv1.5 -o ${CMAKE_BINARY_DIR}/shaders/fog.spv
                  COMMAND glslc ${CMAKE_SOURCE_DIR}/shaders/post_processing.frag --target-spv=spv1.5 -o ${CMAKE_BINARY_DIR}/shaders/post_processing_frag.spv
                  COMMAND glslc ${CMAKE_SOURCE_DIR}/shaders/post_processing.vert --target-spv=spv1.5 -o ${CMAKE_BINARY_DIR}/shaders/post_processing_vert.spv
                  COMMAND glslc ${CMAKE_SOURCE_DIR}/shaders/triplanar.vert --target-spv=spv1.5 -o ${CMAKE_BINARY_DIR}/shaders/triplanar_vert.spv
                  COMMAND glslc ${CMAKE_SOURCE_DIR}/shaders/triplanar.frag --target-spv=spv1.5 -o ${CMAKE_BINARY_DIR}/shaders/triplanar_frag.spv
                  COMMAND glslc ${CMAKE_SOURCE_DIR}/shaders/skybox.vert --target-spv=spv1.5 -o ${CMAKE_BINARY_DIR}/shaders/skybox_vert.spv
                  COMMAND glslc ${CMAKE_SOURCE_DIR}/shaders/skybox.frag --target-spv=spv1.5 -o ${CMAKE_BINARY_DIR}/shaders/skybox_frag.spv
                  COMMAND glslc ${CMAKE_SOURCE_DIR}/shaders/cascaded_shadow.vert --target-spv=spv1.5 -o ${CMAKE_BINARY_DIR}/shaders/cascaded_shadow_vert.spv
                  COMMAND glslc ${CMAKE_SOURCE_DIR}/shaders/cascaded_shadow.frag --target-spv=spv1.5 -o ${CMAKE_BINARY_DIR}/shaders/cascaded_shadow_frag.spv
                  DEPENDS vulkan-test)

add_custom_target(copy-models ALL
                  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/models ${CMAKE_BINARY_DIR}/models
                  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/textures ${CMAKE_BINARY_DIR}/textures
                  DEPENDS vulkan-test)
