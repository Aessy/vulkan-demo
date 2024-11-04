#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_texture_lod : enable
#extension GL_EXT_debug_printf : enable

#define PI 3.1415926535897932384626433832795

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

layout(set = 3, binding = 0) uniform UniformTerrain{
    float max_height;
    uint displacement_map;
    uint normal_map;
    uint texture_id;
    int roughness_texture_id;
    int metallic_texture_id;
    int ao_texture_id;

    uint texture_normal_id;
    float blend_sharpness;

    float shininess;
    float specular_strength;

    float metalness;
    float roughness;
    float ao;

    float texture_scale;
    
    float lod_min;
    float lod_max;
    float weight;
} terrain;

struct ObjectData
{
    mat4 model;
    uint texture_index;
};

layout(std140,set = 2, binding = 0) readonly buffer ObjectBuffer{
    ObjectData objects[];
} ubo2;

layout(location = 0) in vec3 position_worldspace;
layout(location = 1) in vec3 normal;
layout(location = 0) out vec4 out_color;

float findLod()
{
    float distance = length(world.pos - position_worldspace);

    float n = smoothstep(0, terrain.weight, distance);
    float diff = terrain.lod_max-terrain.lod_min;

    return (n*diff)+terrain.lod_min;
}

vec3 getBlending(vec3 world_normal)
{
    vec3 blending = abs(world_normal);
    blending = pow(blending, vec3(terrain.blend_sharpness));
    blending = normalize(max(blending, 0.00001));
    float b = (blending.x + blending.y + blending.z);
    blending /= vec3(b,b,b);
    
    return blending;
}

vec3 phong(vec3 normal, vec3 color)
{
    vec3 light_color = vec3(1,1,1);
    vec3 material_diffuse_color = color;

    vec3 light_dir = normalize(world.light.position - position_worldspace);
    vec3 view_dir = normalize(world.pos - position_worldspace);
    float shininess = terrain.shininess;
    float specular_strength = terrain.specular_strength;

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

vec4 sampleTexture(uint texture_id, float x, float y, float z, vec3 blending, float scale)
{
    vec4 xaxis = texture(texSampler[texture_id], vec2(y,z)*scale);
    vec4 yaxis = texture(texSampler[texture_id], vec2(x,z)*scale);
    vec4 zaxis = texture(texSampler[texture_id], vec2(x,y)*scale);

    return xaxis * blending.x + yaxis * blending.y + zaxis * blending.z;
}

float getRoughness(float x, float y, float z, vec3 blending, float scale)
{
    if (terrain.roughness_texture_id != -1){
        return sampleTexture(terrain.roughness_texture_id, x, y, z, blending, scale).r;
    }
    return terrain.roughness;
}

float getMetallic(float x, float y, float z, vec3 blending, float scale)
{
    if (terrain.metallic_texture_id != -1){
        return sampleTexture(terrain.metallic_texture_id, x, y, z, blending, scale).r;
    }
    return terrain.metalness;
}

float getAo(float x, float y, float z, vec3 blending, float scale)
{
    if (terrain.ao_texture_id!= -1){
        return sampleTexture(terrain.ao_texture_id, x, y, z, blending, scale).r;
    }
    return terrain.ao;
}

void main()
{
    vec3 light_color = vec3(1,1,1);

    float scale = terrain.texture_scale;

    float lod = findLod();
    vec3 n = normalize(normal);

    float y = position_worldspace.y;
    float x = position_worldspace.x;
    float z = position_worldspace.z;
    vec3 blending = getBlending(n);
    
    vec4 tex = sampleTexture(terrain.texture_id, x,y,z, blending, scale);

    float roughness = getRoughness(x,y,z,blending,scale);
    float metallic = getMetallic(x,y,z,blending,scale);
    float ao = getAo(x,y,z,blending,scale);

    vec3 xaxis_normal = 2*texture(texSampler[terrain.texture_normal_id], vec2(y,z)*scale).rgb-1;
    vec3 yaxis_normal = 2*texture(texSampler[terrain.texture_normal_id], vec2(x,z)*scale).rgb-1;
    vec3 zaxis_normal = 2*texture(texSampler[terrain.texture_normal_id], vec2(x,y)*scale).rgb-1;
    
    vec3 normal_tex =  normalize(xaxis_normal * blending.x + yaxis_normal * blending.y + zaxis_normal * blending.z).xyz;
    vec3 final_normal = normalize(normal + normal_tex);
    
    //vec3 result = phong(final_normal, tex.rgb);
    //vec3 result = pbr(final_normal, tex.rgb);
    vec3 result = pbr(final_normal, tex.rgb, roughness, metallic, ao);

    out_color = vec4(result, 1);
}
