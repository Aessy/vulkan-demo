project(vulkan)

cmake_minimum_required(VERSION 3.21)

set(SRC VulkanRenderSystem.cpp
        main.cpp
        height_map.cpp
        Gui.cpp
        Model.cpp
        Program.cpp
        Textures.cpp
        Id.cpp
        imgui_impl_vulkan.cpp
        imgui_impl_glfw.cpp)

add_executable(vulkan-test ${SRC})
target_compile_options(vulkan-test PUBLIC -g -std=c++23)
target_compile_definitions(vulkan-test PUBLIC GLM_ENABLE_EXPERIMENTAL)
target_link_libraries(vulkan-test glfw vulkan X11 imgui)

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
                  DEPENDS vulkan-test)

add_custom_target(copy-models ALL
                  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/models ${CMAKE_BINARY_DIR}/models
                  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/textures ${CMAKE_BINARY_DIR}/textures
                  DEPENDS vulkan-test)
