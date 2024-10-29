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
layout(set = 3, binding = 0) uniform UniformWorld{
    mat4 view;
    mat4 proj;
    vec3 pos;
    LightBufferData light;
} world;

layout(set = 4, binding = 0) uniform Fog{
    uint enabled;
    float base_density;
    float max_density;
    vec3 color;
} fog;

layout(location = 0) in vec2 uv_in;
layout(location = 0) out vec4 fragColor;

vec3 calculateWorldPositionFromDepth(float depth, vec2 frag_uv, mat4 proj, mat4 view) {
    float ndc_x = frag_uv.x * 2.0 - 1.0;  // Map UV [0,1] to NDC [-1,1] for x
    float ndc_y = frag_uv.y * 2.0 - 1.0;  // Map UV [0,1] to NDC [-1,1] for y
    float ndc_z = depth * 2.0 - 1.0;     // Map depth [0,1] to clip space [-1,1]

    // Clip space position.
    vec4 clip_pos= vec4(ndc_x, ndc_y, ndc_z, 1.0);

    // Inverse projection matrix to get view space position
    vec4 view_pos = inverse(proj) * clip_pos;
    // Perform perspective divide to get view space position in 3D
    view_pos /= view_pos.w;
    // Transform view space position to world space using inverse view matrix
    vec4 world_pos = inverse(view) * view_pos;
    return world_pos.xyz;  // Return the 3D world position
}

float raymarch(float distance, vec3 ray_dir, vec3 ray_pos)
{
    float scale_factor = 8.0f;
    int max_steps = 64;

    float step_length = distance/max_steps;

    float accumulated_fog = 0.0f;

    for (int i = 0; i < max_steps; ++i)
    {
        if (   (ray_pos.x > -64*scale_factor && ray_pos.x < 64*scale_factor)
            && (ray_pos.y > -64*scale_factor && ray_pos.y < 64*scale_factor)
            && (ray_pos.z > -64*scale_factor && ray_pos.z < 64*scale_factor))
        {
            float density = texture(fog_sampler, ray_pos/scale_factor).r;
            accumulated_fog += density*step_length;
        }

        ray_pos += (ray_dir*step_length);
    }

    return accumulated_fog;
}

vec4 calculateVolumetricFog(vec4 scene_color)
{
    float depth = texture(depth_sampler, uv_in).r;

    mat4 proj = world.proj;
    mat4 view = world.view;
    vec3 world_position = calculateWorldPositionFromDepth(depth, uv_in, proj, view);

    float distance = length(world_position-world.pos);
    
    vec3 ray_dir = normalize(world_position - world.pos);
    vec3 ray_pos = world.pos;
    
    float accumulated_fog = raymarch(distance, ray_dir, ray_pos);

    float fog_alpha = clamp(0, fog.max_density, accumulated_fog);
    
    return mix(scene_color, vec4(fog.color.xyz, 1), fog_alpha);
}

void main()
{
    ivec2 texture_size = textureSize(color_sampler);
    ivec2 pixel_coord = ivec2(uv_in * texture_size);

    vec4 sampled_color = vec4(0);

    for (int i = 0; i < 8; ++i)
    {
        sampled_color += texelFetch(color_sampler, pixel_coord, i);
    }

    vec4 scene_color = sampled_color/8;

    if (fog.enabled == 1)
    {
        fragColor = calculateVolumetricFog(scene_color);
    }
    else
    {
        fragColor = scene_color;
    }
}
