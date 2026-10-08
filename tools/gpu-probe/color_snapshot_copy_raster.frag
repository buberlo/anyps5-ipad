#version 450
layout(push_constant) uniform Parameters { uint mode; uint width; uint height; uint reserved; } parameters;
layout(location = 0) out vec4 color;
void main() {
    if (parameters.mode == 2u && ((uint(gl_FragCoord.x)+uint(gl_FragCoord.y)) & 1u) != 0u) discard;
    color = vec4(0.125, 0.25, 0.5, 1.0);
}
