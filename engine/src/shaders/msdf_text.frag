#version 450

layout(set = 2, binding = 0) uniform sampler2D u_texture;
layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

float median(float a, float b, float c) {
    return max(min(a, b), min(max(a, b), c));
}

void main() {
    const float distance = median(texture(u_texture, v_uv).r, texture(u_texture, v_uv).g, texture(u_texture, v_uv).b);
    const float alpha = smoothstep(0.46, 0.54, distance);
    out_color = vec4(v_color.rgb, v_color.a * alpha);
}
