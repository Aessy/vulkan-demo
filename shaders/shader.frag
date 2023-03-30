#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform sampler2D texSampler[];

layout(set = 1, binding = 0) uniform UniformLight {
    mat4 view;
    mat4 proj;
    vec3 light_pos;
} light;

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 position_worldspace;
layout(location = 2) in vec3 normal_cameraspace;
layout(location = 3) in vec3 eye_direction_cameraspace;
layout(location = 4) in vec3 light_direction_cameraspace;
layout(location = 5) in flat uint texture_id;
layout(location = 6) in vec3 color;

layout(location = 0) out vec4 out_color;

void main()
{
    vec3 light_color = vec3(1,1,1);
    float light_power = 100.0f;

    vec3 material_diffuse_color = vec3(0.4, 0.1, 0.6); //texture(texSampler[texture_id], uv).rgb;
    vec3 material_ambient_color = vec3(0.3, 0.3, 0.3) * material_diffuse_color;
    vec3 material_speccular_color = vec3(1.0, 1.0, 1.0);

    float distance = length(light.light_pos - position_worldspace);

    vec3 light_dir = normalize(light.light_pos - position_worldspace);
    vec3 n = normalize(normal_cameraspace);

    vec3 l = normalize(light_direction_cameraspace);

    float cos_theta = clamp(dot(n,light_dir), 0,1);

    vec3 R = reflect(-l,n);

    float cos_alpha = clamp(dot(l,R),0,1);

    vec3 color = material_ambient_color +
                 material_diffuse_color * light_color * cos_theta;
    
    out_color = vec4(color, 1);
}
