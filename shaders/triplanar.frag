#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_texture_lod : enable
#extension GL_EXT_debug_printf : enable

#define PI 3.1415926535897932384626433832795

const int DisplacementMap = 1 << 0;
const int DisplacementNormalMap = 1 << 1;
const int RoughnessMap = 1 << 2;
const int MetalnessMap = 1 << 3;
const int AoMap = 1 << 4;
const int AlbedoMap = 1 << 5;
const int NormalMap = 1 << 6;

int TriplanarSampling = 0;
int UvSampling = 1;

int Phong = 0;
int Pbr = 1;

layout(set = 0, binding = 0) uniform sampler2D texSampler[];

struct LightBufferData
{
    vec3 position;
    vec3 light_color;
    vec3 sun_pos;
    float strength;
    float time_of_the_day;
};
layout(set = 1, binding = 0) uniform UniformWorld{
    mat4 view;
    mat4 proj;
    vec3 pos;
    LightBufferData light;
} world;

layout(set = 3, binding = 0) uniform MaterialData{
    int material_features;
    int sampling_mode;
    int shade_mode;
    int displacement_map;
    int displacement_normal_map;
    float displacement_y;
    float shininess;
    float specular_strength;
    int base_color_texture;
    int base_color_normal_texture;
    int roughness_texture;
    int metalness_texture;
    int ao_texture;
    float texture_scale;
    float roughness;
    float metalness;
    float ao;
} material;

struct ObjectData
{
    mat4 model;
    uint texture_index;
};

layout(std140,set = 2, binding = 0) readonly buffer ObjectBuffer{
    ObjectData objects[];
} ubo2;

layout(location = 0) in vec3 position_worldspace;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in mat3 in_TBN;
layout(location = 8) in vec2 in_uv_tex;
layout(location = 9) in vec2 in_uv_normal;

layout(location = 0) out vec4 out_color;

int featureEnabled(int feature)
{
    if ((feature & material.material_features) != 0)
    {
        return 1;
    }

    return 0;
}

vec3 getBlending(vec3 world_normal)
{
    vec3 blending = abs(world_normal);
    blending = pow(blending, vec3(20));
    blending = normalize(max(blending, 0.00001));
    float b = (blending.x + blending.y + blending.z);
    blending /= vec3(b,b,b);
    
    return blending;
}

vec3 phong(vec3 normal, vec3 color)
{
    vec3 sun_dir = normalize(world.light.sun_pos);
    vec3 light_color = vec3(1,1,1);
    vec3 material_diffuse_color = color;

    vec3 light_dir = sun_dir;
    vec3 view_dir = normalize(world.pos - position_worldspace);
    float shininess = material.shininess;
    float specular_strength = material.specular_strength;

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

vec3 pbr(vec3 normal, vec3 albedo, float roughness, float metalness, float ao)
{
    vec3 view_dir = normalize(world.pos - position_worldspace);
    vec3 sun_dir = normalize(world.light.sun_pos);
    vec3 light_dir = sun_dir;
    
    //vec3 light_dir = normalize(world.light.position - position_worldspace);

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

    vec3 kd = mix(vec3(1.0) - F, vec3(0.0), metalness);
    vec3 diffuseBRDF = kd * albedo;


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

vec4 sampleTexture(uint texture_id, vec2 uvX, vec2 uvY, vec2 uvZ, vec3 blending, float scale)
{
    vec4 xaxis = texture(texSampler[texture_id], uvX*scale);
    vec4 yaxis = texture(texSampler[texture_id], uvY*scale);
    vec4 zaxis = texture(texSampler[texture_id], uvZ*scale);

    return xaxis * blending.x + yaxis * blending.y + zaxis * blending.z;
}

float getRoughness(vec2 uvX, vec2 uvY, vec2 uvZ, vec3 blending, float scale)
{
    if (featureEnabled(RoughnessMap) == 1)
    {
        return sampleTexture(material.roughness_texture, uvX, uvY, uvZ, blending, scale).r;
    }
    return material.roughness;
}

float getMetalness(vec2 uvX, vec2 uvY, vec2 uvZ, vec3 blending, float scale)
{
    if (featureEnabled(MetalnessMap) == 1)
    {
        return sampleTexture(material.metalness_texture, uvX, uvY, uvZ, blending, scale).r;
    }
    return material.metalness;
}

float getAo(vec2 uvX, vec2 uvY, vec2 uvZ, vec3 blending, float scale)
{
    if (featureEnabled(AoMap) == 1)
    {
        return sampleTexture(material.ao_texture, uvX, uvY, uvZ, blending, scale).r;
    }
    return material.ao;
}

vec3 uvSampling()
{
    float scale = material.texture_scale;
    vec3 albedo = vec3(1,0,0);
    vec3 normal = vec3(0,1,0);

    if (featureEnabled(AlbedoMap) == 1)
    {
        albedo = texture(texSampler[material.base_color_texture], in_uv_tex*scale).rgb;
    }
    if (featureEnabled(NormalMap) == 1)
    {
        normal = normalize(2*texture(texSampler[material.base_color_normal_texture], in_uv_normal*scale).rgb-1.0f);
    }

    // Put texture normal into TBN space. It is ready for use.
    normal = normalize(in_TBN * normal);

    // Shade with phong algorithm
    if (material.shade_mode == Phong)
    {
        return phong(normal, albedo);
    }
    // Shade with PBR algorithm
    else if (material.shade_mode == Pbr)
    {
        float roughness = material.roughness;
        float metalness = material.metalness;
        float ao = material.ao;
    
        if (featureEnabled(RoughnessMap) == 1)
        {
            roughness = texture(texSampler[material.roughness_texture], in_uv_tex*scale).r;
        }
        if (featureEnabled(MetalnessMap) == 1)
        {
            metalness = texture(texSampler[material.metalness_texture], in_uv_tex*scale).r;
        }
        if (featureEnabled(AoMap) == 1)
        {
            ao = texture(texSampler[material.ao_texture], in_uv_tex*scale).r;
        }

        return pbr(normal, albedo, roughness, metalness, ao);
    }
}

vec3 triplanarSampling()
{
    float x = position_worldspace.x;
    float y = position_worldspace.y;
    float z = position_worldspace.z;
    float scale = material.texture_scale;

    vec2 uvX = vec2(z,y);
    vec2 uvY = vec2(x,z);
    vec2 uvZ = vec2(x,y);
    vec3 normal = normalize(in_normal);
    vec3 blending = getBlending(normal);

    vec3 albedo = sampleTexture(material.base_color_texture, uvX,uvY,uvZ, blending, scale).rgb;

    vec3 final_normal = normal;
    if (featureEnabled(NormalMap) == 1)
    {
        // UDN Blend
        vec3 xaxis = normalize(2*texture(texSampler[material.base_color_normal_texture], uvX*scale).rgb-1);
        vec3 yaxis = normalize(2*texture(texSampler[material.base_color_normal_texture], uvY*scale).rgb-1);
        vec3 zaxis = normalize(2*texture(texSampler[material.base_color_normal_texture], uvZ*scale).rgb-1);

        vec3 world_normal_x = xaxis.zyx;
        vec3 world_normal_y = yaxis.xzy;
        vec3 world_normal_z = zaxis;

        vec3 normal_x = vec3(xaxis.xy + final_normal.zy, final_normal.x);
        vec3 normal_y = vec3(yaxis.xy + final_normal.xz, final_normal.y);
        vec3 normal_z = vec3(zaxis.xy + final_normal.xy, final_normal.z);

        final_normal = normalize(
            normal_x.zyx * blending.x +
            normal_y.xzy * blending.y +
            normal_z.xyz * blending.z
        );
    }
    if (material.shade_mode == Phong)
    {
        return phong(final_normal, albedo);
    }
    else if (material.shade_mode == Pbr)
    {
        float roughness = getRoughness(uvX,uvY,uvZ,blending,scale);
        float metalness = getMetalness(uvX,uvY,uvZ,blending,scale);
        float ao        = getAo(uvX,uvY,uvY,blending,scale);

        return pbr(final_normal, albedo, roughness, metalness, ao);
    }

    return vec3(0,0,0);
}

void main()
{
    if (material.sampling_mode == UvSampling)
    {
        out_color = vec4(uvSampling(), 1);
    }
    else if (material.sampling_mode == TriplanarSampling)
    {
        out_color = vec4(triplanarSampling(), 1);
    }
}
