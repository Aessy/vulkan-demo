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

layout(location = 0) in vec2 uv_in;
layout(location = 0) out vec4 fragColor;

vec3 calculateWorldPositionFromDepth(float depth, vec2 fragUV, mat4 proj, mat4 view) {
    // Reconstruct the clip space position
    float ndcX = fragUV.x * 2.0 - 1.0;  // Map UV [0,1] to NDC [-1,1] for x
    float ndcY = fragUV.y * 2.0 - 1.0;  // Map UV [0,1] to NDC [-1,1] for y
    float ndcZ = depth * 2.0 - 1.0;     // Map depth [0,1] to clip space [-1,1]

    // Clip space position (homogeneous coordinates)
    vec4 clipPos = vec4(ndcX, ndcY, ndcZ, 1.0);

    // Inverse projection matrix to get view space position
    vec4 viewPos = inverse(proj) * clipPos;
    // Perform perspective divide to get view space position in 3D
    viewPos /= viewPos.w;
    // Transform view space position to world space using inverse view matrix
    vec4 worldPos = inverse(view) * viewPos;
    return worldPos.xyz;  // Return the 3D world position
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

    float depth = texture(depth_sampler, uv_in).r;

    mat4 proj = world.proj;
    mat4 view = world.view;
    vec3 world_position = calculateWorldPositionFromDepth(depth, uv_in, proj, view);

    float distance = length(world_position-world.pos);

    vec3 ray_dir = normalize(world_position - world.pos);
    vec3 ray_pos = world.pos;

    int max_steps = 64;

    float step_size = 1.0f;
    float accumulated_fog = 0.0;

    float distance_traveled = 0.0f;

    int i = 0;

    // Sample the fog 64 times
    for (i = 0; i < max_steps; ++i)
    {
        ray_pos += (ray_dir*step_size);

        if (   (ray_pos.x > -64 && ray_pos.x < 64)
            && (ray_pos.y > -64 && ray_pos.y < 64)
            && (ray_pos.z > -64 && ray_pos.z < 64))
        {
            float density = texture(fog_sampler, ray_pos).r;
            if (distance_traveled > distance)
            {
                float remaining_distance = distance_traveled - distance;
                remaining_distance /= step_size;    // remaining distance is now 0-1, hopefully
                density *= 1-remaining_distance;
                accumulated_fog += density;
                break;
            }

            accumulated_fog += density;
        }

        distance_traveled += step_size;

    }

    float fog_alpha = clamp(0, 0.9, accumulated_fog);

    vec4 scene_color = sampled_color/8;
    vec4 fog_color = vec4(1.0, 1.0, 1.0, 1.0);

    vec4 final_color = mix(scene_color, fog_color, fog_alpha);
    fragColor = final_color;
}
