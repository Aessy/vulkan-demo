#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_texture_lod : enable
#extension GL_EXT_debug_printf : enable

layout(set = 0, binding = 0) uniform sampler2D texSampler[];

struct LightBufferData
{
    vec3 position;
    vec3 light_color;
    float strength;
};
layout(set = 1, binding = 0) uniform UniformWorld{
    mat4 view;
    mat4 proj;
    vec3 pos;
    LightBufferData light;
} world;

layout(set = 3, binding = 0) uniform UniformTerrain{
    float max_height;
    uint displacement_map;
    uint normal_map;
    uint texture_id;
    
    float lod_min;
    float lod_max;
    float weight;
} terrain;

struct ObjectData
{
    mat4 model;
    uint texture_index;
};

layout(std140,set = 2, binding = 0) readonly buffer ObjectBuffer{
    ObjectData objects[];
} ubo2;

layout(location = 0) in vec3 position_worldspace;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec2 normal_coord;
layout(location = 4) in vec3 tangent;
layout(location = 5) in vec3 bitangent;

layout(location = 0) out vec4 out_color;

float findLod()
{
    float distance = length(world.pos - position_worldspace);

    float n = smoothstep(0, terrain.weight, distance);
    float diff = terrain.lod_max-terrain.lod_min;

    return (n*diff)+terrain.lod_min;
}

void main()
{
    vec3 light_color = vec3(1,1,1);

    mat3 TBN = transpose(mat3(tangent, normal, bitangent));

    float lod = findLod();
    vec3 n = normalize(normal);

    vec3 material_diffuse_color = textureLod(texSampler[terrain.texture_id], uv, lod).xyz;
    vec3 material_ambient_color = vec3(0.3, 0.3, 0.3) * material_diffuse_color;

    vec3 light_dir = normalize(world.light.position);

    float cos_theta = clamp(dot(n,light_dir), 0.1,1);

    vec3 color = material_diffuse_color * light_color * cos_theta;

    out_color = vec4(color, 1);
    //out_color = vec4(normal_color, 1);
}
