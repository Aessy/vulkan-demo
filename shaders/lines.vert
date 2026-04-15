#version 460

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in float inParam;

struct LightBufferData {
    vec3  position;
    vec3  light_color;
    vec3  sun_pos;
    float strength;
    float time_of_the_day;
};

layout(set = 0, binding = 0) uniform UniformWorld {
    mat4            view;
    mat4            proj;
    vec3            pos;
    LightBufferData light;
} world;

struct ObjectData {
    mat4 model;
    uint texture_index;
};

layout(std430, set = 1, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
} model_buf;

layout(location = 0) out vec4 v_color;
layout(location = 1) out float v_param;

void main()
{
    ObjectData obj = model_buf.objects[gl_BaseInstance];
    vec4 world_pos = obj.model * vec4(inPos, 1.0);
    gl_Position = world.proj * world.view * world_pos;
    v_color = inColor;
    v_param  = inParam;
}
