#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_texture_lod : enable
#extension GL_EXT_debug_printf : enable

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
    uint texture_normal_id;
    float blend_sharpness;

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

void main()
{
    vec3 light_color = vec3(1,1,1);

    float scale = terrain.texture_scale;

    float lod = findLod();
    vec3 n = normalize(normal);

    float y = position_worldspace.y;
    float x = position_worldspace.x;
    float z = position_worldspace.z;
    
    vec4 xaxis = textureLod(texSampler[terrain.texture_id], vec2(y,z)*scale, lod);
    vec4 yaxis = textureLod(texSampler[terrain.texture_id], vec2(x,z)*scale, lod);
    vec4 zaxis = textureLod(texSampler[terrain.texture_id], vec2(x,y)*scale, lod);

    vec3 blending = getBlending(n);
    vec4 tex = xaxis * blending.x + yaxis * blending.y + zaxis * blending.z;

    vec3 xaxis_normal = 2*textureLod(texSampler[terrain.texture_normal_id], vec2(y,z)*scale, lod).rgb-1;
    vec3 yaxis_normal = 2*textureLod(texSampler[terrain.texture_normal_id], vec2(x,z)*scale, lod).rgb-1;
    vec3 zaxis_normal = 2*textureLod(texSampler[terrain.texture_normal_id], vec2(x,y)*scale, lod).rgb-1;
    
    vec3 normal_tex =  normalize(xaxis_normal * blending.x + yaxis_normal * blending.y + zaxis_normal * blending.z).xyz;

    vec3 material_diffuse_color = tex.rgb;
    vec3 material_ambient_color = vec3(0.1, 0.1, 0.1) * material_diffuse_color;

    vec3 final_normal = normalize(normal + normal_tex);

    vec3 light_dir = normalize(world.light.position - position_worldspace);
    float cos_theta = clamp(dot(final_normal,light_dir), 0.1,1);
    vec3 color = material_diffuse_color * light_color * cos_theta;

    out_color = vec4(color, 1);
}
