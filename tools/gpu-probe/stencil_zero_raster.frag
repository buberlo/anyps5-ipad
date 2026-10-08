#version 450
layout(push_constant) uniform Parameters { uint group; uint reverse; uint discardEnabled; uint unused; } parameters;
layout(location = 0) out vec4 color;
void main() {
    uvec2 pixel = uvec2(floor(gl_FragCoord.xy));
    if (parameters.discardEnabled != 0u && ((pixel.x + pixel.y) & 3u) == 0u) discard;
    color = vec4(gl_FrontFacing ? 73.0 : 173.0, float((pixel.x * 13u + pixel.y * 7u) & 255u),
                 float(29u + parameters.group * 103u), 255.0) / 255.0;
}
