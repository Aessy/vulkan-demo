#version 460 core
layout (location = 0) in vec3 in_position;

struct ObjectData
{
    mat4 model;
    uint texture_index;
};

layout(set = 0, binding = 0) uniform ShadowMapBuffer{
    mat4 light_matrix;
} shadow_map;

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

layout(std430, set = 2, binding = 0) readonly buffer ObjectBuffer{
    ObjectData objects[];
} ubo2;

void main()
{
    ObjectData ubo = ubo2.objects[gl_BaseInstance];
    gl_Position = shadow_map.light_matrix * ubo.model * vec4(in_position, 1.0);
    gl_Position.y = -gl_Position.y;
}