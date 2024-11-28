#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_texture_lod : enable
#extension GL_EXT_debug_printf : enable

layout(location = 0) out vec2 uv_out;

void main()
{
    vec3 coords[6];
    coords[0] = vec3(-1.0, -1.0, 0.0); // Bottom left
    coords[1] = vec3( 1.0, -1.0, 0.0); // Bottom right
    coords[2] = vec3( 1.0,  1.0, 0.0); // Top right
    coords[3] = vec3( 1.0,  1.0, 0.0); // Top right
    coords[4] = vec3(-1.0,  1.0, 0.0); // top left
    coords[5] = vec3( -1.0,-1.0, 0.0); // bottom left

    vec2 uv_frag[6];
    uv_frag[0] = vec2(0, 1);
    uv_frag[1] = vec2(1, 1);
    uv_frag[2] = vec2(1, 0);
    uv_frag[3] = vec2(1, 0);
    uv_frag[4] = vec2(0, 0);
    uv_frag[5] = vec2(0, 1);

    gl_Position = vec4(coords[gl_VertexIndex], 1.0);
    uv_out = uv_frag[gl_VertexIndex];
}
