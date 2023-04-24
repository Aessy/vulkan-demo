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
    float texture_scale;

    float lod_min;
    float lod_max;
    float weight;
} terrain;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec2 in_tex_coord;
layout(location = 3) in vec3 in_normal;
layout(location = 4) in vec2 in_normal_coord;
layout(location = 5) in vec3 in_tangent;
layout(location = 6) in vec3 in_bitangent;

layout(location = 0) out vec3 position_worldspace;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec2 uv;
layout(location = 3) out vec2 normal_coord;
layout(location = 4) out vec3 tangent;
layout(location = 5) out vec3 bitangent;

void main()
{
    ObjectData ubo = ubo2.objects[gl_BaseInstance];
    vec3 pos = inPosition;
    normal_coord = in_normal_coord;
    
    uv = in_tex_coord;

    vec4 displace = texture(texSampler[terrain.displacement_map], normal_coord);
    pos.y += displace.r * terrain.max_height;

    vec3 normal_color = in_normal; //normalize(2*texture(texSampler[terrain.normal_map], normal_coord).rbg-1.0);

    gl_Position = world.proj * world.view * ubo.model * vec4(pos, 1.0);

    mat3 inv_trans = inverse(transpose(mat3(ubo.model)));

    position_worldspace = (ubo.model * vec4(pos,          1)).xyz;
    tangent             = (ubo.model * vec4(normalize(in_tangent),   1)).xyz;
    bitangent           = (ubo.model * vec4(normalize(in_bitangent), 1)).xyz;
    normal              = (inv_trans * normal_color).xyz;
}
