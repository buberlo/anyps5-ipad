#version 450
layout(push_constant) uniform Parameters { uint group; uint reverse; uint discardEnabled; uint unused; } parameters;
void main() {
    const vec2 corners[4] = vec2[4](vec2(-0.9, -0.8), vec2(0.6, -0.8), vec2(0.6, 0.75), vec2(-0.9, 0.75));
    const uint indices[6] = uint[6](0, 1, 2, 0, 2, 3);
    uint vertex = uint(gl_VertexIndex);
    if (parameters.reverse != 0u && vertex % 3u != 0u) vertex += vertex % 3u == 1u ? 1u : -1u;
    gl_Position = vec4(corners[indices[vertex]], 0.5, 1.0);
}
