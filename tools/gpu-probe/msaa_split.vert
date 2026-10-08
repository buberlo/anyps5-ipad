#version 450
layout(location = 0) out vec2 center;
void main() {
    const vec2 vertices[3] = vec2[3](vec2(20, 18), vec2(215, 43), vec2(63, 229));
    center = vertices[gl_VertexIndex] / 16.0;
    gl_Position = vec4(center / 8.0 - 1.0, 0, 1);
}
