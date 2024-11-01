#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform sampler2D texSampler[];

#define PI 3.1415926535897932384626433832795

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

struct ObjectData
{
    mat4 model;
    uint texture_index;

    int shading_style;
    float shininess;
    float specular_strength;
    float roughness;
    float metallness;
    float ao;
};

layout(location = 0) in vec3 position_worldspace;
layout(location = 1) in vec3 normal;
layout(location = 2) in flat uint texture_id;
layout(location = 3) in vec2 uv;
layout(location = 4) in flat ObjectData object_data;

layout(location = 0) out vec4 out_color;

vec3 phong(vec3 normal, vec3 color, float shininess, float specular_strength)
{
    vec3 light_color = vec3(1,1,1);
    vec3 material_diffuse_color = color;

    vec3 light_dir = normalize(world.light.position - position_worldspace);
    vec3 view_dir = normalize(world.pos - position_worldspace);

    // Ambient
    float ambient_strength = 0.1f;
    vec3 ambient_component = ambient_strength * light_color;

    // Diffuse
    float cos_theta = clamp(dot(normal,light_dir), 0.1,1);
    vec3 diffuse_component = cos_theta * light_color;

    // Specular
    vec3 reflect_dir = reflect(-light_dir, normal);
    float spec = pow(max(dot(view_dir, reflect_dir), 0.0), shininess);
    vec3 specular_component = specular_strength * spec * light_color;

    vec3 result = (ambient_component + diffuse_component + specular_component) * material_diffuse_color;

    return result;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    // Fresnel approximation for reflectance
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float GGX_NDF(vec3 normal, vec3 halfVector, float roughness) {
    float alpha = roughness * roughness; // Square the roughness
    float NdotH = max(dot(normal, halfVector), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    // Denominator of GGX NDF
    float denom = NdotH2 * (alpha * alpha - 1.0) + 1.0;
    denom = PI * denom * denom; // Factor in PI to make it a proper distribution

    return alpha * alpha / denom; // Return the GGX NDF
}

float geometrySchlickGGX(float NdotV, float roughness)
{
    // Schlick's approximation for geometry function
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    // Combined geometry function
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 pbr(vec3 normal, vec3 albedo, float metalness, float roughness, float ao)
{
    vec3 view_dir = normalize(world.pos - position_worldspace);
    vec3 light_dir = normalize(world.light.position - position_worldspace);
    vec3 light_color = world.light.light_color;
    float light_intensity = world.light.strength;

    vec3 H = normalize(view_dir + light_dir);

    // Calculate reflectance at normal incidence (F0)
    vec3 F0 = mix(vec3(0.04), albedo, metalness);    // Dielectric base reflectance is 0.04

    // Fresnel term
    vec3 F = fresnelSchlick(max(dot(H, view_dir), 0.0), F0);

    // Normal Distribution Function (NDF)
    float NDF = GGX_NDF(normal, H, roughness);

    // Geometry function
    float G = geometrySmith(normal, view_dir, light_dir, roughness);

    vec3 specular = (NDF * G * F) / max(4.0 * max(dot(normal, view_dir), 0.0) * max(dot(normal, light_dir), 0.0), 0.001);

    // Diffuse reflection (Lambertian)
    vec3 kD = vec3(1.0) - F; // Diffuse reflection is reduced by metalness
    kD *= 1.0 - metalness;   // No diffuse reflection for metallic surfaces
    vec3 diffuse = albedo / PI;

    // Final color calculation with ambient occlusion (AO)
    vec3 ambient = ao * albedo * 0.1; // Ambient color

    vec3 reflection = F * albedo * ao * 0.05;

    vec3 color = (ambient + reflection + (kD * diffuse + specular) * max(dot(normal, light_dir), 0.0) * light_color * light_intensity);
    return color;
}
void main()
{
    vec3 material_diffuse_color = vec3(1,0,0);
    vec3 n = normalize(normal);

    if (object_data.shading_style == 0)
    {
        out_color = vec4(phong(n, material_diffuse_color, object_data.shininess, object_data.specular_strength), 1.0f);
    }
    else if (object_data.shading_style == 1)
    {
        out_color = vec4(pbr(n, material_diffuse_color, object_data.metallness, object_data.roughness, object_data.ao), 1.0f);
    }
}