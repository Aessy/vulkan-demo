#pragma once

#include "Textures.h"
#include "Mesh.h"
#include "Program.h"
#include "Scene.h"
#include "Model.h"

#include <vector>
#include <map>
#include <memory>

struct Application
{
    Textures textures;
    Models models;
    Meshes meshes;
    std::vector<std::unique_ptr<Program>> programs;
    Scene scene;
};