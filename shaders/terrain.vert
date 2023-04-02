#version 460

layout(set = 0, binding = 0) uniform UniformWorld{
    mat4 view;
    mat4 proj;
    vec3 light_pos;
} world;

layout(set = 1, binding = 0) uniform UniformObjectData {
    mat4 model;
    uint texture_index;
} object_data;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec2 in_tex_coord;
layout(location = 3) in vec3 in_normal;

layout(location = 0) out vec3 position_worldspace;
layout(location = 1) out vec3 normal;

void main()
{
    gl_Position = world.proj * world.view * object_data.model * vec4(inPosition, 1.0);

    position_worldspace = (object_data.model * vec4(inPosition, 1)).xyz;
    normal = in_normal;
}
