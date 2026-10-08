#version 450
layout(set=0, binding=0) uniform texture2D sourceImage;
layout(set=0, binding=6) uniform sampler sourceSampler;
layout(set=0, binding=9, std430) readonly buffer Parameters { uvec4 add; } parameters;
layout(location=0) out vec4 color;
void main() {
    vec2 extent = vec2(textureSize(sampler2D(sourceImage, sourceSampler), 0));
    uvec4 inputBytes = uvec4(round(textureLod(sampler2D(sourceImage, sourceSampler),
                                           gl_FragCoord.xy / extent, 0.0) * 255.0));
    color = vec4((inputBytes + parameters.add) & uvec4(255)) / 255.0;
}
