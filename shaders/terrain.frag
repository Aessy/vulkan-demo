#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform sampler2D texSampler[];

struct LightBufferData
{
    vec3 position;
    vec3 light_color;
    float strength;
};
layout(set = 1, binding = 0) uniform UniformWorld{
    mat4 view;
    mat4 proj;
    LightBufferData light;
} world;

layout(set = 3, binding = 0) uniform UniformTerrain{
    float max_height;
    uint displacement_map;
    uint normal_map;
    uint texture_id;
} terrain;

layout(location = 0) in vec3 position_worldspace;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec4 out_color;

void main()
{
    vec3 light_color = vec3(1,1,1);

    vec3 material_diffuse_color = texture(texSampler[terrain.texture_id], uv).rgb;
    vec3 material_ambient_color = vec3(0.3, 0.3, 0.3) * material_diffuse_color;

    vec3 n = normalize(normal);
    vec3 light_dir = normalize(vec3(-1,-1,0)); //normalize(world.light.position - position_worldspace);

    float cos_theta = max(dot(n,light_dir), 0.1);

    vec3 color = material_diffuse_color * light_color * cos_theta;

    out_color = vec4(color, 1);
}
