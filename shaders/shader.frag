#version 450

layout(set = 0, binding = 0) uniform sampler2D texSampler[];

layout(set = 1, binding = 0) uniform UniformLight {
    vec3 light_pos;
} light;

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 position_worldspace;
layout(location = 2) in vec3 normal_cameraspace;
layout(location = 3) in vec3 eye_direction_cameraspace;
layout(location = 4) in vec3 light_direction_cameraspace;

layout(location = 0) out vec4 out_color;

void main()
{
    vec3 light_color = vec3(1,1,1);
    float light_power = 50.0f;


    vec3 material_diffuse_color = texture(texSampler[0], uv).rgb;
    vec3 material_ambient_color = vec3(0.1, 0.1, 0.1) * material_diffuse_color;
    vec3 material_speccular_color = vec3(0.3, 0.3, 0.3);

    float distance = length(light.light_pos - position_worldspace);

    vec3 n = normalize(normal_cameraspace);

    vec3 l = normalize(light_direction_cameraspace);

    float cos_theta = clamp(dot(n,l), 0,1);

    vec3 E = normalize(eye_direction_cameraspace);
    vec3 R = reflect(-l,n);

    float cos_alpha = clamp(dot(E,R),0,1);

    vec3 color = material_ambient_color +
                 material_diffuse_color * light_color * cos_theta / (distance * distance);
                 material_speccular_color * light_color * light_power * pow(cos_alpha,5) / (distance*distance);
    
    out_color = vec4(color, 1);
}
