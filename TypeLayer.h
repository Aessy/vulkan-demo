#pragma once

#include "Material.h"
#include "Model.h"
#include <array>
#include <vector>

#include <vulkan/vulkan_core.h>

// Types here  work as a translation layer between GUI and rendering enginee
namespace layer_types
{

enum class BindingType : int
{
    Uniform = 0,
    Storage = 1,
    TextureSampler = 2,
    StorageImage = 3
};

enum class BufferType : int
{
    ModelBufferObject,
    WorldBufferObject,
    TerrainBufferObject,
    FogVolumeObject,
    PostProcessingDataBufferObject,
    MaterialShaderData,
    AtmosphereShaderData,
    NoBuffer,
};

enum class PolygonMode : int
{
    Fill,
    Line
};

struct Binding
{
    std::array<char, 50> name;
    int binding{};
    BindingType type = BindingType::Uniform; // uniform, storage buffer, or texture sampler
    int size{};
    bool vertex = false;
    bool fragment = false;
    bool tess_ctrl = false;
    bool tess_evu = false;
    bool compute = true;
    vk::ImageView storage_image;
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
    std::array<char, 50> tesselation_ctrl_shader{{}};
    std::array<char, 50> tesselation_evaluation_shader{{}};
    std::array<char, 50> compute_shader{{}};

    PolygonMode polygon_mode = PolygonMode::Fill;

    std::vector<Buffer> buffers;
};

struct RenderingSystem
{
    std::vector<Program> programs;
};

}
