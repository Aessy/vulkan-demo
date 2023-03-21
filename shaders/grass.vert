#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 1, binding = 0) uniform WorldBuffer{
    mat4 view;
    mat4 proj;
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

layout(location = 0) out float height;

void main() {
    ObjectData ubo = ubo2.objects[gl_InstanceIndex];
    gl_Position = world.proj * world.view * ubo.model * vec4(inPosition, 1.0);

    height = inPosition.y / 0.60;
}
