#version 460

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_debug_printf : enable

layout(vertices = 3) out;
 
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

layout(location = 0) in vec3 control_position_worldspace[];
layout(location = 1) in vec3 control_normal[];
layout(location = 2) in vec2 control_uv[];
layout(location = 3) in vec2 control_normal_coord[];
layout(location = 4) in mat3 control_TBN[];
layout(location = 10) in mat4 control_model[];

layout(location = 0) out vec3 evaluation_position_worldspace[];
layout(location = 1) out vec3 evaluation_normal[];
layout(location = 2) out vec2 evaluation_uv[];
layout(location = 3) out vec2 evaluation_normal_coord[];
layout(location = 4) out mat3 evaluation_TBN[];
layout(location = 10) out mat4 evaluation_model[];

void main()
{   
    evaluation_position_worldspace[gl_InvocationID] = control_position_worldspace[gl_InvocationID];
    evaluation_normal[gl_InvocationID] = control_normal[gl_InvocationID];
    evaluation_uv[gl_InvocationID] = control_uv[gl_InvocationID];
    evaluation_normal_coord[gl_InvocationID] = control_normal_coord[gl_InvocationID];
    evaluation_TBN[gl_InvocationID] = control_TBN[gl_InvocationID];
    evaluation_model[gl_InvocationID] = control_model[gl_InvocationID];

    if (gl_InvocationID == 0)
    {
        vec3 position1 = control_position_worldspace[0];
        vec3 position2 = control_position_worldspace[1];
        vec3 position3 = control_position_worldspace[2];
 
        //vec3 middlePoint1 = GetMiddlePoint(position2, position3);
        //vec3 middlePoint2 = GetMiddlePoint(position3, position1);
        //vec3 middlePoint3 = GetMiddlePoint(position1, position2);
 
        //vec3 middleOfTriangle = GetMiddlePoint(middlePoint1, middlePoint2, middlePoint3);
 
        float distance = length(world.pos - position1);

        gl_TessLevelInner[0] = 1;
        gl_TessLevelOuter[0] = 1;
        gl_TessLevelOuter[1] = 1;
        gl_TessLevelOuter[2] = 1;

        if (distance < 70)
        {
            gl_TessLevelInner[0] = 3;
            gl_TessLevelOuter[0] = 4;
            gl_TessLevelOuter[1] = 4;
            gl_TessLevelOuter[2] = 4;
        }
    }
}