#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(set = 0, binding = 2) uniform UniformLight {
    vec3 light_pos;
} light;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec2 in_tex_coord;
layout(location = 3) in vec3 in_normal;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec3 position_worldspace;
layout(location = 2) out vec3 normal_cameraspace;
layout(location = 3) out vec3 eye_direction_cameraspace;
layout(location = 4) out vec3 light_direction_cameraspace;

//const vec3 light_position_worldspace = vec3(1, 0, 1);

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);

    position_worldspace = (ubo.model * vec4(inPosition, 1)).xyz;

    vec3 vertex_position_cameraspace = (ubo.view * ubo.model * vec4(inPosition, 1)).xyz;
    eye_direction_cameraspace = vec3(0,0,0) - vertex_position_cameraspace;

    vec3 light_position_cameraspace = (ubo.view * vec4(light.light_pos,1)).xyz;
    light_direction_cameraspace = light_position_cameraspace + eye_direction_cameraspace;

    normal_cameraspace = (ubo.view * ubo.model * vec4(in_normal,0)).xyz;

    uv = in_tex_coord;
}
