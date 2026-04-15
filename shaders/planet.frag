#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform sampler2D texSampler[];

struct PlanetMaterial {
    int   diffuse_texture;
    int   normal_texture;
    int   has_normal_map;
    int   has_atmosphere;
    vec4  atmosphere_color_scale;
    vec4  albedo_color;
    float roughness;
    float metallic;
    float emissive;
    float _pad0;
};

layout(std430, set = 3, binding = 0) readonly buffer PlanetMaterialBuffer {
    PlanetMaterial objects[];
} planet_mats;

layout(location = 0) in vec3 frag_pos;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec3 sun_dir;
layout(location = 3) in vec2 frag_uv;
layout(location = 7) in flat int instance;
layout(location = 8) in float frag_w;

layout(location = 0) out vec4 out_color;

void main()
{
    PlanetMaterial mat = planet_mats.objects[instance];
    vec3 albedo = mat.diffuse_texture >= 0
        ? texture(texSampler[mat.diffuse_texture], frag_uv).rgb
        : mat.albedo_color.rgb;

    vec3 N = normalize(frag_normal);
    vec3 L = normalize(sun_dir);  // direction from camera toward sun

    float emissive = mat.emissive;
    float diffuse  = max(dot(N, L), 0.0);
    float lighting = mix(diffuse, 1.0, emissive);

    // Logarithmic depth
    const float FAR = 1e10;
    gl_FragDepth = log2(max(1e-6, 1.0 + frag_w)) / log2(FAR + 1.0);

    out_color = vec4(albedo * lighting, 1.0);
}
