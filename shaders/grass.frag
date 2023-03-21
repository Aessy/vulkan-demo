#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(  push_constant ) uniform UniformBufferObject {
    mat4 model;
} ubo;

layout(set = 0, binding = 0) uniform sampler2D texSampler[];
layout(location = 0) out vec4 out_color;

layout(location = 0) in float height;

void main()
{
    out_color = vec4(mix(vec3(0, 0.2, 0), vec3(0.4, 0.9, 0.2), vec3(height)), 1);
}
