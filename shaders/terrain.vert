#version 460
#extension GL_EXT_nonuniform_qualifier : require

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
} terrain;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec2 in_tex_coord;
layout(location = 3) in vec3 in_normal;

layout(location = 0) out vec3 position_worldspace;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec2 uv;
layout(location = 3) out vec2 tex_coord;

void main()
{
    ObjectData ubo = ubo2.objects[gl_BaseInstance];

    tex_coord = in_tex_coord;

    vec2 uv_top_left = vec2(0,1);
    vec2 uv_top_right = vec2(1,1);
    vec2 uv_bottom_left = vec2(0,0);
    vec2 uv_bottom_right = vec2(1,0);

    vec2 uvs[4];
    uvs[0] = uv_top_left;
    uvs[1] = uv_top_right;
    uvs[2] = uv_bottom_left;
    uvs[3] = uv_bottom_right;

    vec2 uv_pos = uvs[gl_VertexIndex%4];

    vec3 pos = inPosition;

    normal = vec3(0,0,0);
    
    uv = uv_pos;


    vec4 displace = texture(texSampler[terrain.displacement_map], in_tex_coord);
    pos.y += displace.r * terrain.max_height;

    gl_Position = world.proj * world.view * ubo.model * vec4(pos, 1.0);

    position_worldspace = (ubo.model * vec4(pos, 1)).xyz;
}
