#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_debug_printf : enable
precision highp float;

struct LightBufferData
{
    vec3 position;
    vec3 light_color;
    vec3 sun_dir;
    float strength;
    float time_of_the_day;
};
layout(set = 0, binding = 0) uniform UniformWorld{
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

layout(std140,set = 2, binding = 0) readonly buffer ObjectBuffer{
    ObjectData objects[];
} ubo2;

layout(location = 0) in vec3 in_position;
layout(location = 0) out vec3 frag_dir;

void main()
{
    ObjectData ubo = ubo2.objects[gl_BaseInstance];

    mat3 view_rotation_only = inverse(mat3(world.view));
    frag_dir = (ubo.model * vec4(in_position, 1)).xyz;

    gl_Position = world.proj * world.view * ubo.model * vec4(in_position, 1.0);
}