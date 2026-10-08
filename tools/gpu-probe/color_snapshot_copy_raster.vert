#version 450
layout(push_constant) uniform Parameters { uint mode; uint width; uint height; uint reserved; } parameters;
void main() {
    const vec2 partial[3] = vec2[3](vec2(-1,-1),vec2(1,-1),vec2(-1,1));
    const vec2 full[3] = vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    gl_Position = vec4(parameters.mode == 3u ? full[gl_VertexIndex] : partial[gl_VertexIndex], 0, 1);
}
