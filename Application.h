#pragma once

#include "Textures.h"
#include "Mesh.h"
#include "Program.h"
#include "Scene.h"
#include "Model.h"
#include "PostProcessing.h"

#include <memory>
#include <vector>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>

struct Application
{
    Textures textures;
    Models models;
    Meshes meshes;
    std::vector<std::unique_ptr<Program>> programs;
    Scene scene;

    PostProcessing ppp;

};