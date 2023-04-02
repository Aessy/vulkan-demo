#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform UniformWorld {
    mat4 view;
    mat4 proj;
    vec3 light_pos;
} world;

layout(location = 0) in vec3 position_worldspace;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec4 out_color;

void main()
{
    vec3 light_color = vec3(1,1,1);

    vec3 material_diffuse_color = vec3(0.4, 0.1, 0.6);
    vec3 material_ambient_color = vec3(0.3, 0.3, 0.3) * material_diffuse_color;

    vec3 light_dir = normalize(world.light_pos - position_worldspace);
    vec3 n = normalize(normal);

    float cos_theta = clamp(dot(n,light_dir), 0,1);

    vec3 color = material_ambient_color +
                 material_diffuse_color * light_color * cos_theta;

    out_color = vec4(color, 1);
}
