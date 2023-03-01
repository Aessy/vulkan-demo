# !/bin/env bash

glslc shaders/shader.frag --target-spv=spv1.6 -o shaders/frag.spv
glslc shaders/shader.vert --target-spv=spv1.6 -o shaders/vert.spv
glslc shaders/grass.frag --target-spv=spv1.6 -o shaders/grass_frag.spv
glslc shaders/grass.vert --target-spv=spv1.6 -o shaders/grass_vert.spv

g++ main.cpp VulkanRenderSystem.cpp -lglfw -lvulkan -lX11 -g -std=c++23
