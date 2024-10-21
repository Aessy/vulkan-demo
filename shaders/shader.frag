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
    vec3 pos;
    LightBufferData light;
} world;

layout(location = 0) in vec3 position_worldspace;
layout(location = 1) in vec3 normal;
layout(location = 2) in flat uint texture_id;
layout(location = 3) in vec2 uv;

layout(location = 0) out vec4 out_color;

void main()
{
    vec3 light_color = world.light.light_color;
    float light_power = world.light.strength;
    vec3 light_pos = world.light.position;
    vec3 light_dir = normalize(light_pos - position_worldspace);
    //vec3 material_diffuse_color = texture(texSampler[texture_id], uv).rgb;
    vec3 material_diffuse_color = vec3(1,0,0);
    vec3 n = normalize(normal);

    vec3 ambient = 0.1f * light_color;

    float diffuse_factor = max(dot(n, light_dir), 0.0);
    vec3 diffuse = diffuse_factor * light_color;

    vec3 result = (ambient + diffuse) * material_diffuse_color;
    out_color = vec4(result, 1);
    return;



    vec3 material_ambient_color = vec3(0.3, 0.3, 0.3) * material_diffuse_color;
    vec3 material_speccular_color = vec3(1.0, 1.0, 1.0);

    float distance = length(light_pos - position_worldspace);

    float cos_theta = clamp(dot(n,light_dir), 0,1);

    vec3 color = material_ambient_color +
                 material_diffuse_color * light_color * cos_theta * light_power / (distance*distance);
    
    out_color = vec4(1,0,0,1);
}