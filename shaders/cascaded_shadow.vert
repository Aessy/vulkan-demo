#version 460 core
layout (location = 0) in vec3 in_position;

struct ObjectData
{
    mat4 model;
    uint texture_index;
};

layout(set = 1, binding = 0) uniform ShadowMapBuffer{
    mat4 light_matrix;
} shadow_map;

layout(std430, set = 0, binding = 0) readonly buffer ObjectBuffer{
    ObjectData objects[];
} ubo2;

    

void main()
{
    ObjectData ubo = ubo2.objects[gl_BaseInstance];
    gl_Position = shadow_map.light_matrix * ubo.model* vec4(in_position, 1.0);
}