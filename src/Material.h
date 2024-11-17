#pragma once

#include <string>
#include <array>


struct PhongParameters
{
    // Shininess and specular reflection 0-1 for phong shading
    float shininess;
    float specular_strength;
};

struct PbrParameters
{
    int roughness_texture;
    int metallic_texture;
    int ao_texture;

    // Roughness, metalloc, and AO levels 0-1 when texture maps is not used.
    float roughness;
    float metallic;
    float ao;
};

struct Lod
{
    float lod_base_distance = 10.0f;
    float lod_scale_factor = 0.5f;
};

// Specifies what maps are available and should be used.
enum MaterialFeatureFlag : int
{
    DisplacementMap = 1 << 0,
    DisplacementNormalMap = 1 << 1,
    RoughnessMap = 1 << 2,
    MetalnessMap = 1 << 3,
    AoMap = 1 << 4,
    AlbedoMap = 1 << 5,
    NormalMap = 1 << 6,
};

enum SamplingMode : int
{
    TriplanarSampling = 0,
    UvSampling = 1
};

enum ReflectionShadeMode : int
{
    Phong = 0,
    Pbr = 1
};
struct MaterialShaderData
{
    // Landscape rendering supports phong and PBR
    int material_features;
    int sampling_mode;
    int shade_mode;

    // Displacement map and normal for the terrain
    int displacement_map_texture;
    int normal_map_texture;
    float displacement_y{0};

    // Shininess and specular reflection 0-1 for phong shading
    float shininess{2};
    float specular_strength{0.5};

    // Texture maps
    int base_color_texture;
    int base_color_normal_texture;
    int roughness_texture;
    int metallic_texture;
    int ao_texture;

    // Scaling factor for the texture maps
    float scaling_factor{1.0f};

    // Roughness, metalloc, and AO levels 0-1 when texture maps is not used.
    float roughness;
    float metallic;
    float ao;
};

static_assert(sizeof(MaterialShaderData) == 68, "");

struct Material
{
    std::array<char, 30> name{{}};

    // Should be set to the shader for landscape
    int program{};

    MaterialShaderData shader_data;
};

struct StandardMaterial
{
    std::array<char, 30> name{{}};

    // Should be set to the shader for standard PBR
    int program{};

    // Standard material shading supports phong and PBR
    ReflectionShadeMode mode{ReflectionShadeMode::Phong};

    // Shininess and specular reflection 0-1 for phong shading
    float shininess{0};
    float specular_strength{0};

    // Texture maps
    int base_color_texture{0};
    int base_color_normal_texture{0};
    int roughness_texture{0};
    int metallic_texture{0};
    int ao_texture{0};

    // Roughness, metalloc, and AO levels 0-1 when texture maps is not used.
    float roughness{0};
    float metallic{0};
    float ao{0};
};