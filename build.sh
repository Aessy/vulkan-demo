# !/bin/env bash

glslc shaders/shader.frag -o shaders/frag.spv
glslc shaders/shader.vert -o shaders/vert.spv

g++ main.cpp -lglfw -lvulkan -lX11 -g -std=c++23
