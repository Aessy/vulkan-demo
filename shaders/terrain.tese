#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_debug_printf : enable


layout (triangles, equal_spacing, ccw) in;

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

layout(location = 0) in vec3 evaluation_position_worldspace[];
layout(location = 1) in vec3 evaluation_normal[];
layout(location = 2) in vec2 evaluation_uv[];
layout(location = 3) in vec2 evaluation_normal_coord[];
layout(location = 4) in mat3 evaluation_TBN[];
layout(location = 10) in mat4 evaluation_model[];

layout(location = 0) out vec3 position_worldspace;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec2 uv;
layout(location = 3) out vec2 normal_coord;
layout(location = 4) out mat3 TBN;

void main()
{
    mat4 model = evaluation_model[0];

    vec3 pos = (gl_TessCoord.x * evaluation_position_worldspace[0] + gl_TessCoord.y * evaluation_position_worldspace[1] + gl_TessCoord.z * evaluation_position_worldspace[2]);

    normal = (gl_TessCoord.x * evaluation_normal[0] + gl_TessCoord.y * evaluation_normal[1] + gl_TessCoord.z * evaluation_normal[2]);
    uv = (gl_TessCoord.x * evaluation_uv[0] + gl_TessCoord.y * evaluation_uv[1] + gl_TessCoord.z * evaluation_uv[2]);
    normal_coord = (gl_TessCoord.x * evaluation_normal_coord[0] + gl_TessCoord.y * evaluation_normal_coord[1] + gl_TessCoord.z * evaluation_normal_coord[2]);
    TBN = (gl_TessCoord.x * evaluation_TBN[0] + gl_TessCoord.y * evaluation_TBN[1] + gl_TessCoord.z * evaluation_TBN[2]);

    vec4 displace = texture(texSampler[terrain.displacement_map], normal_coord);
    pos.y = displace.r * terrain.max_height;

    gl_Position = world.proj * world.view * model * vec4(pos, 1.0);
    position_worldspace = (model * vec4(pos, 1)).xyz;
}