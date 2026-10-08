#version 450
// Three input corners; the production rect-list helper creates the fourth.
out gl_PerVertex { vec4 gl_Position; };
layout(push_constant) uniform Bounds { vec4 rectangle; } bounds;
void main() {
    vec2 corner = bounds.rectangle.xy;
    if (gl_VertexIndex == 1) corner.x = bounds.rectangle.z;
    if (gl_VertexIndex == 2) corner.y = bounds.rectangle.w;
    gl_Position = vec4(corner, 0.0, 1.0);
}
