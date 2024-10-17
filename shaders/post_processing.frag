#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_texture_lod : enable
#extension GL_EXT_debug_printf : enable

layout(set = 0, binding = 0) uniform sampler2DMS color_sampler;
layout(set = 1, binding = 0) uniform sampler3D fog_sampler;
layout(set = 2, binding = 0) uniform sampler2D depth_sampler;

struct LightBufferData
{
    vec3 position;
    vec3 light_color;
    float strength;
};
layout(set = 2, binding = 0) uniform UniformWorld{
    mat4 view;
    mat4 proj;
    vec3 pos;
    LightBufferData light;
} world;

layout(location = 0) in vec2 uv_in;
layout(location = 0) out vec4 fragColor;

void main()
{
    ivec2 texture_size = textureSize(color_sampler);
    ivec2 pixel_coord = ivec2(uv_in * texture_size);

    vec4 sampled_color = vec4(0);

    for (int i = 0; i < 4; ++i)
    {
        sampled_color += texelFetch(color_sampler, pixel_coord, i);
    }

    fragColor = sampled_color/4;
}
