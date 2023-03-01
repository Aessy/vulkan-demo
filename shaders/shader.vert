#version 460

layout(set = 1, binding = 0) uniform UniformLight {
    mat4 view;
    mat4 proj;
    vec3 light_pos;
} world;

struct ObjectData
{
    mat4 model;
    uint texture_index;
};

layout(std140,set = 2, binding = 0) readonly buffer ObjectBuffer{
    ObjectData objects[];
} ubo2;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec2 in_tex_coord;
layout(location = 3) in vec3 in_normal;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec3 position_worldspace;
layout(location = 2) out vec3 normal_cameraspace;
layout(location = 3) out vec3 eye_direction_cameraspace;
layout(location = 4) out vec3 light_direction_cameraspace;

void main() {
    ObjectData ubo = ubo2.objects[gl_BaseInstance];
    gl_Position = world.proj * world.view * ubo.model * vec4(inPosition, 1.0);

    position_worldspace = (ubo.model * vec4(inPosition, 1)).xyz;

    vec3 vertex_position_cameraspace = (world.view * ubo.model * vec4(inPosition, 1)).xyz;
    eye_direction_cameraspace = vec3(0,0,0) - vertex_position_cameraspace;

    vec3 light_position_cameraspace = (world.view * vec4(world.light_pos,1)).xyz;
    light_direction_cameraspace = light_position_cameraspace + eye_direction_cameraspace;

    normal_cameraspace = (world.view * ubo.model * vec4(in_normal,0)).xyz;

    uv = in_tex_coord;
}
