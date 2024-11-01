#version 460

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
    uint texture_normal;
    uint texture_roughness;
    uint texture_ao;

    int shading_style;
    float shininess;
    float specular_strength;
    float roughness;
    float metallness;
    float ao;
};

layout(std140,set = 2, binding = 0) readonly buffer ObjectBuffer{
    ObjectData objects[];
} ubo2;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 in_tex_coord;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec3 in_tangent;
layout(location = 4) in vec3 in_bitangent;

layout(location = 0) out vec3 position_worldspace;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec2 uv;
layout(location = 3) out mat3 TBN;
layout(location = 6) out ObjectData object_data;

void main() {
    ObjectData ubo = ubo2.objects[gl_BaseInstance];
    gl_Position = world.proj * world.view * ubo.model * vec4(inPosition, 1.0);

    mat3 inv_trans = inverse(transpose(mat3(ubo.model)));
    vec3 T = normalize(vec3(ubo.model * vec4(in_tangent, 0)));
    vec3 B = normalize(vec3(ubo.model * vec4(in_bitangent, 0)));
    vec3 N = normalize(inv_trans * in_normal);
    TBN = mat3(T, B, N);

    position_worldspace = (ubo.model * vec4(inPosition, 1)).xyz;
    normal = (inv_trans * in_normal);
    uv = in_tex_coord;
    object_data = ubo;
}
