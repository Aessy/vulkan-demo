#version 450

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 position_worldspace;
layout(location = 2) in vec3 normal_cameraspace;
layout(location = 3) in vec3 eye_direction_cameraspace;
layout(location = 4) in vec3 light_direction_cameraspace;

layout(location = 0) out vec4 out_color;

const vec3 light_position_worldspace = vec3(1, 0, 1);

void main()
{
    vec3 light_color = vec3(1,1,1);
    float light_power = 50.0f;


    vec3 material_diffuse_color = texture(texSampler, uv).rgb;
    vec3 material_ambient_color = vec3(0.1, 0.1, 0.1) * material_diffuse_color;
    vec3 material_speccular_color = vec3(0.3, 0.3, 0.3);

    float distance = length(light_position_worldspace - position_worldspace);

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

    //vec4 result = vec4(frag_color, 1) * texture(texSampler, frag_tex_coord);
    //out_color = result;
}
