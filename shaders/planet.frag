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
    int   cloud_texture;
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

    if (mat.cloud_texture >= 0)
    {
        float cloud = texture(texSampler[mat.cloud_texture], frag_uv).r;
        albedo = mix(albedo, vec3(1.0), cloud * 0.9);
    }

    vec3 N = normalize(frag_normal);
    vec3 L = normalize(sun_dir);  // direction from camera toward sun

    float emissive = mat.emissive;
    float diffuse  = max(dot(N, L), 0.0);
    float lighting = mix(diffuse, 1.0, emissive);

    vec3 color = albedo * lighting;

    // Atmosphere rim glow
    if (mat.has_atmosphere == 1)
    {
        vec3 V = normalize(-frag_pos);
        float rim = 1.0 - max(dot(N, V), 0.0);
        rim = pow(rim, 3.0);
        // Only glow on the lit side
        float atm_strength = mat.atmosphere_color_scale.w * diffuse;
        color = mix(color, mat.atmosphere_color_scale.rgb, rim * atm_strength);
    }

    // Logarithmic depth
    const float FAR = 1e10;
    gl_FragDepth = log2(max(1e-6, 1.0 + frag_w)) / log2(FAR + 1.0);

    out_color = vec4(color, 1.0);
}
