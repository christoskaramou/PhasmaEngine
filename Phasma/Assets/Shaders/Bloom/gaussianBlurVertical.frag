#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout (set = 0, binding = 0) uniform sampler2D gaussianBlurHorizontalSampler;
layout(push_constant) uniform Constants { float brightness; float intensity; float range; float exposure; } values;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

void main()
{
    ivec2 texDim = textureSize(gaussianBlurHorizontalSampler, 0);
    float texelSize = values.range / float(texDim.y);

    vec2 coords[11];
    for (int i=-5; i<= 5; i++){
        coords[i+5] = inUV + vec2(0.0, texelSize * i);
    }

    outColor = vec4(0.0);
    outColor += texture(gaussianBlurHorizontalSampler, coords[0]) * 0.0093;
    outColor += texture(gaussianBlurHorizontalSampler, coords[1]) * 0.028002;
    outColor += texture(gaussianBlurHorizontalSampler, coords[2]) * 0.065984;
    outColor += texture(gaussianBlurHorizontalSampler, coords[3]) * 0.121703;
    outColor += texture(gaussianBlurHorizontalSampler, coords[4]) * 0.175713;
    outColor += texture(gaussianBlurHorizontalSampler, coords[5]) * 0.198596;
    outColor += texture(gaussianBlurHorizontalSampler, coords[6]) * 0.175713;
    outColor += texture(gaussianBlurHorizontalSampler, coords[7]) * 0.121703;
    outColor += texture(gaussianBlurHorizontalSampler, coords[8]) * 0.065984;
    outColor += texture(gaussianBlurHorizontalSampler, coords[9]) * 0.028002;
    outColor += texture(gaussianBlurHorizontalSampler, coords[10]) * 0.0093;
    outColor.w = 1.0;
}
