#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 in_tex_coord;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec2 in_normal_coord;
layout(location = 4) in vec3 in_tangent;
layout(location = 5) in vec3 in_bitangent;

layout(set = 1, binding = 0) uniform UniformWorld {
    mat4 view;
    mat4 proj;
    vec3 camera_pos;
    // LightBufferObject (std140 layout):
    vec3  light_position; float _p0;
    vec3  light_color;    float _p1;
    vec3  sun_pos;        // normalized direction from camera toward sun
    float strength;
    float time_of_day;
} world;

struct ObjectData {
    mat4 model;
    uint texture_index;
};

layout(std430, set = 2, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
} model_buf;

layout(location = 0) out vec3 frag_pos;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec3 sun_dir;
layout(location = 7) out flat int instance;
layout(location = 8) out float frag_w;

void main()
{
    ObjectData obj = model_buf.objects[gl_BaseInstance];
    vec4 world_pos = obj.model * vec4(inPosition, 1.0);
    gl_Position = world.proj * world.view * world_pos;

    frag_pos    = world_pos.xyz;
    frag_normal = normalize(mat3(obj.model) * in_normal);
    sun_dir     = world.sun_pos;  // direction from camera toward sun (world space)
    instance    = gl_BaseInstance;
    frag_w      = gl_Position.w;
}
