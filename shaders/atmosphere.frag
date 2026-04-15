#version 460

// Per-planet atmosphere color and scale packed into a vec4
// xyz = atmosphere color, w = scale/strength
layout(std430, set = 2, binding = 0) readonly buffer AtmosphereBuffer {
    vec4 data[];  // one entry per planet: xyz=color, w=scale
} atm_buf;

layout(location = 0) in vec3 frag_pos;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec3 sun_dir;
layout(location = 7) in flat int instance;
layout(location = 8) in float frag_w;
layout(location = 9) in flat int planet_index;

layout(location = 0) out vec4 out_color;

void main()
{
    vec4  atm      = atm_buf.data[planet_index];
    vec3  atm_color = atm.rgb;
    float strength  = atm.w;

    vec3 N = normalize(frag_normal);
    vec3 V = normalize(-frag_pos);  // view direction (camera at origin)
    vec3 L = normalize(sun_dir);

    // Rim: strongest at silhouette edge, zero at center
    float rim = 1.0 - max(dot(N, V), 0.0);
    rim = pow(rim, 5.0);

    // Only show atmosphere on the lit side — fade toward terminator
    float sun_facing = dot(N, L) * 0.5 + 0.5;  // remap [-1,1] to [0,1]

    float alpha = rim * strength * sun_facing;

    // Logarithmic depth
    const float FAR = 1e10;
    gl_FragDepth = log2(max(1e-6, 1.0 + frag_w)) / log2(FAR + 1.0);

    out_color = vec4(atm_color, alpha);
}
