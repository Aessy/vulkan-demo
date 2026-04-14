#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 in_tex_coord;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec2 in_normal_coord;
layout(location = 4) in vec3 in_tangent;
layout(location = 5) in vec3 in_bitangent;

struct LightBufferData {
    vec3  position;
    vec3  light_color;
    vec3  sun_pos;
    float strength;
    float time_of_the_day;
};

layout(set = 1, binding = 0) uniform UniformWorld {
    mat4           view;
    mat4           proj;
    vec3           pos;
    LightBufferData light;
} world;

struct ObjectData {
    mat4 model;
    uint texture_index;
};

layout(std430, set = 2, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
} model_buf;

layout(location = 0) out vec3 frag_pos;
layout(location = 1) out vec2 frag_uv;
layout(location = 2) out vec3 frag_normal;
layout(location = 3) out mat3 TBN;      // uses locations 3, 4, 5
// location 6 unused
layout(location = 7) out flat int instance;
layout(location = 8) out float frag_w;

void main()
{
    ObjectData obj = model_buf.objects[gl_BaseInstance];
    mat4 model = obj.model;

    vec4 world_pos = model * vec4(inPosition, 1.0);
    gl_Position = world.proj * world.view * world_pos;

    frag_pos = world_pos.xyz;
    frag_uv  = in_tex_coord;

    // World-space TBN
    vec3 N = normalize(mat3(model) * in_normal);
    vec3 T = normalize(mat3(model) * in_tangent);
    T = normalize(T - dot(T, N) * N);  // re-orthogonalise
    vec3 B = cross(N, T);

    TBN        = mat3(T, B, N);
    frag_normal = N;

    instance = gl_BaseInstance;
    frag_w   = gl_Position.w;
}
