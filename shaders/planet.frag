#version 460
#extension GL_EXT_nonuniform_qualifier : require

#define PI 3.14159265358979323846

layout(set = 0, binding = 0) uniform sampler2D texSampler[];

struct LightBufferData {
    vec3  position;
    vec3  light_color;
    vec3  sun_pos;
    float strength;
    float time_of_the_day;
};

layout(set = 1, binding = 0) uniform UniformWorld {
    mat4           view;
    mat4           proj;
    vec3           pos;
    LightBufferData light;
} world;

struct PlanetMaterial {
    int   diffuse_texture;
    int   normal_texture;
    int   has_normal_map;
    int   has_atmosphere;          // 16 bytes
    vec4  atmosphere_color_scale;  // 32 bytes  (.w = rim scale)
    vec4  albedo_color;            // 48 bytes
    float roughness;               // 52 bytes
    float metallic;                // 56 bytes
    float emissive;                // 60 bytes
    float _pad0;                   // 64 bytes
};

layout(std430, set = 3, binding = 0) readonly buffer PlanetMaterialBuffer {
    PlanetMaterial objects[];
} planet_mats;

layout(location = 0) in vec3 frag_pos;
layout(location = 1) in vec2 frag_uv;
layout(location = 2) in vec3 frag_normal;
layout(location = 3) in mat3 TBN;    // locations 3, 4, 5
// location 6 unused
layout(location = 7) in flat int instance;
layout(location = 8) in float frag_w;

layout(location = 0) out vec4 out_color;

// ---------- PBR helpers (verbatim from triplanar.frag) ----------

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}

float GGX_NDF(vec3 N, vec3 H, float roughness)
{
    float alpha  = roughness * roughness;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom  = NdotH2 * (alpha * alpha - 1.0) + 1.0;
    denom = PI * denom * denom;
    return alpha * alpha / denom;
}

float geometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

// ----------------------------------------------------------------

void main()
{
    PlanetMaterial mat = planet_mats.objects[instance];

    // Albedo
    vec3 albedo = mat.albedo_color.rgb;
    if (mat.diffuse_texture >= 0)
        albedo = texture(texSampler[mat.diffuse_texture], frag_uv).rgb;

    // Normal
    vec3 N = normalize(frag_normal);
    if (mat.has_normal_map == 1)
    {
        vec3 n = 2.0 * texture(texSampler[mat.normal_texture], frag_uv).rgb - 1.0;
        vec3 candidate = normalize(TBN * n);
        // Fallback to geometry normal if normal points away from camera
        vec3 V_check = normalize(-frag_pos);
        if (dot(candidate, V_check) > 0.0)
            N = candidate;
    }

    // View and light directions (camera is at origin in CRR)
    vec3 V = normalize(-frag_pos);
    vec3 L = normalize(world.light.sun_pos);
    vec3 H = normalize(V + L);

    float roughness = mat.roughness;
    float metallic  = mat.metallic;

    // PBR
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F  = fresnelSchlick(max(dot(H, V), 0.0), F0);
    float NDF = GGX_NDF(N, H, roughness);
    float G   = geometrySmith(N, V, L, roughness);

    vec3 specular = (NDF * G * F) /
        max(4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0), 0.001);

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    vec3 ambient = albedo * 0.03;

    float NdotL = max(dot(N, L), 0.0);
    vec3 color = ambient +
                 (diffuse + specular) * world.light.light_color * world.light.strength * NdotL;

    // Atmosphere rim effect
    if (mat.has_atmosphere == 1)
    {
        float rim = 1.0 - max(0.0, dot(V, N));
        rim = pow(rim, 3.0);
        float scale = mat.atmosphere_color_scale.w;
        color = mix(color, mat.atmosphere_color_scale.rgb, rim * scale * 0.7);
    }

    // Emissive override (e.g. Sun): mix PBR/tonemapped with raw albedo
    vec3 pbr_tonemapped = color / (color + vec3(1.0));
    color = mix(pbr_tonemapped, albedo, mat.emissive);

    // Logarithmic depth buffer
    const float FAR = 1e10;
    gl_FragDepth = log2(max(1e-6, 1.0 + frag_w)) / log2(FAR + 1.0);

    out_color = vec4(color, 1.0);
}
