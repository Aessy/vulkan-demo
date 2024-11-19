#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_debug_printf : enable

const int DisplacementMap = 1 << 0;
const int DisplacementNormalMap = 1 << 1;
const int RoughnessMap = 1 << 2;
const int MetalnessMap = 1 << 3;
const int AoMap = 1 << 4;
const int AlbedoMap = 1 << 5;
const int NormalMap = 1 << 6;

int TriplanarSampling = 0;
int UvSampling = 1;

int Phong = 0;
int Pbr = 1;

layout(set = 0, binding = 0) uniform sampler2D texSampler[];

struct LightBufferData
{
    vec3 position;
    vec3 light_color;
    vec3 sun_pos;
    float strength;
    float time_of_the_day;
};
layout(set = 1, binding = 0) uniform UniformWorld{
    mat4 view;
    mat4 proj;
    vec3 pos;
    LightBufferData light;
} world;

struct ObjectData
{
    mat4 model;
    uint texture_index;
};

layout(std430,set = 2, binding = 0) readonly buffer ObjectBuffer{
    ObjectData objects[];
} ubo2;

struct MaterialData
{
    int material_features;
    int sampling_mode;
    int shade_mode;
    int displacement_map;
    int displacement_normal_map;
    float displacement_y;
    float shininess;
    float specular_strength;
    int base_color_texture;
    int base_color_normal_texture;
    int roughness_texture;
    int metallic_texture;
    int ao_texture;
    float texture_scale;
    float roughness;
    float metallic;
    float ao;
};

layout(std430, set = 3, binding = 0) readonly buffer MaterialBufferObject{
    MaterialData objects[];
} materials;

layout(set = 4, binding = 0) uniform UniformShadowLightViews{
    mat4 shadow_light_views[5];
} shadow_light_views;

layout(set = 5, binding = 0) uniform sampler2D shadow_map_sampler[5];

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 in_tex_coord;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec2 in_normal_coord;
layout(location = 4) in vec3 in_tangent;
layout(location = 5) in vec3 in_bitangent;

layout(location = 0) out vec3 position_worldspace;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out mat3 TBN;
layout(location = 8) out vec2 uv_tex;
layout(location = 9) out vec2 uv_normal;
layout(location = 10) out int instance;

MaterialData material;

void main()
{
    ObjectData ubo = ubo2.objects[gl_BaseInstance];
    material = materials.objects[gl_BaseInstance];

    vec3 pos = inPosition;
    
    // Displace the vertex if a vertex map is included
    if ((material.material_features & DisplacementMap) != 0)
    {
        vec4 displace = texture(texSampler[material.displacement_map], in_normal_coord);
        float displacement = displace.r * material.displacement_y;
        pos.y = displacement;
    }

    vec3 normal = in_normal;

    // Sample vertex normal from normal map if included
    if ((material.material_features & DisplacementNormalMap) != 0)
    {
        normal = normalize(2*texture(texSampler[material.displacement_normal_map], in_normal_coord).rbg-1.0);
    }

    // The inverse transpose model matrix is used for putting the vertex normal into model space
    mat3 inv_trans = inverse(transpose(mat3(ubo.model)));

    // For UV sampling we require the tangent and bitangent to be present and generate the TBN output
    if (material.sampling_mode == UvSampling)
    {
        vec3 T = normalize(vec3(ubo.model * vec4(in_tangent, 0)));
        vec3 B = normalize(vec3(ubo.model * vec4(in_bitangent, 0)));
        vec3 N = normalize(inv_trans * in_normal);
        TBN = mat3(T, B, N);
    }
    else
    {
        TBN = mat3(1.0f);
    }

    gl_Position = world.proj * world.view * ubo.model * vec4(pos, 1.0);
    position_worldspace = (ubo.model * vec4(pos,1)).xyz;

    out_normal = (inv_trans * normal);
    uv_tex = in_tex_coord;
    uv_normal = in_tex_coord;
    instance = gl_BaseInstance;
}
