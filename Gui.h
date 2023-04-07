#pragma once

#include <imgui.h>

#include <array>
#include <vector>
#include <string>

namespace gui
{

struct Binding
{
    std::array<char, 50> name;
    int binding = 0;
    int type = 0; // uniform, storage buffer, or texture sampler
    int size = 0;
    bool vertex = false;
    bool fragment = false;
};

struct Buffer
{
    std::array<char, 50> name;
    int type {};

    int size;
    Binding binding{};
};

struct Program
{
    std::array<char, 50> name{{}};
    std::array<char, 50> vertex_shader{{}};
    std::array<char, 50> fragment_shader{{}};

    std::vector<Buffer> buffers;
};

struct RenderingSystem
{
    std::vector<Binding> bindings;
    std::vector<Buffer> buffers;

    std::vector<Program> programs;
};

void createBinding(RenderingSystem& system);
void createGui(RenderingSystem& system);

}
