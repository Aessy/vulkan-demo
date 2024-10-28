#version 460

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

struct ObjectData
{
    mat4 model;
    uint texture_index;
};

layout(std140,set = 2, binding = 0) readonly buffer ObjectBuffer{
    ObjectData objects[];
} ubo2;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 in_tex_coord;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec3 position_worldspace;
layout(location = 1) out vec3 normal;
layout(location = 2) out flat uint texture_id;
layout(location = 3) out vec2 uv;

void main() {
    ObjectData ubo = ubo2.objects[gl_BaseInstance];
    gl_Position = world.proj * world.view * ubo.model * vec4(inPosition, 1.0);

    position_worldspace = (ubo.model * vec4(inPosition, 1)).xyz;
    normal = (ubo.model * vec4(in_normal.xyz, 1)).xyz;
    texture_id = ubo.texture_index;
    uv = in_tex_coord;
}
