#pragma once

#include "Textures.h"
#include "Mesh.h"
#include "Program.h"
#include "Scene.h"

#include <vector>
#include <map>
#include <memory>

struct Application
{
    Textures textures;
    std::map<std::string, DrawableMesh> meshes;
    std::vector<std::unique_ptr<Program>> programs;
    Scene scene;
};