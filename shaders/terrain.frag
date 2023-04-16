#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_texture_lod : enable

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
    
    float lod_min;
    float lod_max;
    float weight;
} terrain;

layout(location = 0) in vec3 position_worldspace;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec2 normal_coord;

layout(location = 0) out vec4 out_color;

float findLod()
{
    float distance = length(world.pos - position_worldspace);

    float n = smoothstep(0, terrain.weight, distance);
    float diff = terrain.lod_max-terrain.lod_min;

    return (n*diff)+terrain.lod_min;

    // Dist = 200
    // Weight = 400

    // lod_min = 3
    // lod_max = 6

    // n = 0.5
    //
    // (n * (lod_max-lod_min))+lod_min
    // lod = 4.5 
}

void main()
{
    vec3 light_color = vec3(1,1,1);

    vec3 normal_color = 2.0 * texture(texSampler[terrain.normal_map], normal_coord).rgb - 1.0;
    vec3 n = normalize(normal_color);

    float lod = findLod();

    vec3 material_diffuse_color = textureLod(texSampler[terrain.texture_id], uv, lod).xyz; //texture(texSampler[terrain.texture_id], uv).rgb;
    vec3 material_ambient_color = vec3(0.3, 0.3, 0.3) * material_diffuse_color;

    vec3 light_dir = normalize(-world.light.position);


    float cos_theta = 0.1;
    if (world.light.position.y >= -80) 
    {
        cos_theta = clamp(dot(n,light_dir), 0.1,1);
    }

    vec3 color = material_diffuse_color * light_color * cos_theta;

    out_color = vec4(color, 1);
    //out_color = vec4(normal_color, 1);
}
