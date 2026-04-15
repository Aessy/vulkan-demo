#version 460

layout(location = 0) in vec4 v_color;
layout(location = 1) in float v_param;

layout(push_constant) uniform PC {
    float dash_count;   // 0 = solid; >0 = dashes per loop
    float alpha;        // per-object alpha override (multiplied with vertex alpha)
} pc;

layout(location = 0) out vec4 fragColor;

void main()
{
    if (pc.dash_count > 0.0 && fract(v_param * pc.dash_count) < 0.5)
        discard;
    fragColor = vec4(v_color.rgb, v_color.a * pc.alpha);
}
