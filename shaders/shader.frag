#version 450

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec2 frag_tex_coord;
layout(location = 2) in vec3 frag_uv;
layout(location = 3) in vec3 frag_pos;

layout(location = 0) out vec4 out_color;

void main()
{
    vec4 light_color = vec4(1,1,1,1);
    vec3 light_pos = vec3(-1000,0,0);

    vec3 normal = normalize(frag_uv);
    vec3 light_dir = normalize(light_pos - frag_pos);

    float diffuse = max(dot(normal, light_dir), 0.0);

    out_color = texture(texSampler, frag_tex_coord) * light_color * diffuse;
}
