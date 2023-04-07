#pragma once

#include <array>
#include <vector>

// Types here  work as a translation layer between GUI and rendering enginee
namespace layer_types
{

enum class BindingType : int
{
    Uniform = 0,
    Storage = 1,
    TextureSampler = 2
};

enum class BufferType : int
{
    ModelBufferObject,
    WorldBufferObject
};

struct Binding
{
    std::array<char, 50> name;
    int binding{};
    BindingType type = BindingType::Uniform; // uniform, storage buffer, or texture sampler
    int size{};
    bool vertex = false;
    bool fragment = false;
};

struct Buffer
{
    std::array<char, 50> name{{}};
    BufferType type = BufferType::ModelBufferObject;

    int size{};
    Binding binding{};
};

struct Program
{
    int id{};

    std::array<char, 50> name{{}};
    std::array<char, 50> vertex_shader{{}};
    std::array<char, 50> fragment_shader{{}};

    std::vector<Buffer> buffers;
};

struct RenderingSystem
{
    std::vector<Program> programs;
};

}