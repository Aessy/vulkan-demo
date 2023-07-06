#version 460
#extension GL_EXT_nonuniform_qualifier : require
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

struct ObjectData
{
    mat4 model;
    uint texture_index;
};

layout(std140,set = 2, binding = 0) readonly buffer ObjectBuffer{
    ObjectData objects[];
} ubo2;

layout(set = 3, binding = 0) uniform UniformTerrain{
    float max_height;
    uint displacement_map;
    uint normal_map;
    uint texture_id;
    float blend_sharpness;
    float texture_scale;
    float lod_min;
    float lod_max;
    float weight;
} terrain;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 in_tex_coord;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec2 in_normal_coord;
layout(location = 4) in vec3 in_tangent;
layout(location = 5) in vec3 in_bitangent;

layout(location = 0) out vec3 position_worldspace;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec2 uv;
layout(location = 3) out vec2 normal_coord;
layout(location = 4) out mat3 TBN;
layout(location = 10) out mat4 model;


vec3 calc_initvec(vec3 n)
{
    vec3 v2 = vec3(0,0,1);
    if (n.x > n.y && n.x < n.z)
    {
        v2 = vec3(1,0,0);
    }
    else if (n.y < n.z)
    {
        v2 = vec3(0,1,0);
    }

    vec3 init_vec = cross(n, v2);

    return init_vec;
}

void main()
{
    ObjectData ubo = ubo2.objects[gl_BaseInstance];
    
    vec3 pos = inPosition;
    normal_coord = in_normal_coord;
    
    uv = in_tex_coord;

    vec4 displace = texture(texSampler[terrain.displacement_map], normal_coord);
    pos.y = displace.r * terrain.max_height;

    vec3 normal_color = normalize(2*texture(texSampler[terrain.normal_map], normal_coord).rbg-1.0);

    vec3 init_vec = calc_initvec(normal_color);
    vec3 tangent = normalize(init_vec - dot(normal_color, init_vec) * normal_color);
    vec3 bitangent = normalize(cross(normal_color, tangent));

    gl_Position = world.proj * world.view * ubo.model * vec4(pos, 1.0);

    mat3 inv_trans = inverse(transpose(mat3(ubo.model)));

    vec3 T = normalize(vec3(inv_trans * tangent));
    vec3 B = normalize(vec3(inv_trans * bitangent));
    vec3 N = normalize(vec3(inv_trans * normal_color));
    TBN = transpose(mat3(T, B, N));

    position_worldspace = (ubo.model * vec4(pos,          1)).xyz;
    normal              = (inv_trans * normal_color).xyz;
    model = ubo.model;
}
