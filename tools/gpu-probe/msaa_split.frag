#version 450
layout(location = 0) in vec2 center;
layout(location = 0) out float value;
layout(push_constant) uniform Parameters { uint gradient; } parameters;
void main() {
    value = parameters.gradient == 0 ? 1.0 : (center.x + 2.0 * center.y) / 64.0;
}
