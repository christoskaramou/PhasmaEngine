/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#version 450

layout (set = 0, binding = 0) uniform sampler2D brightFilterSampler;
layout(push_constant) uniform Constants { float brightness; float intensity; float range; float exposure; } values;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

void main()
{
    ivec2 texDim = textureSize(brightFilterSampler, 0);
    float texelSize = values.range / float(texDim.x);

    vec2 coords[11];
    for (int i=-5; i<= 5; i++){
        coords[i+5] = inUV + vec2(texelSize * i, 0.0);
    }

    outColor = vec4(0.0);
    outColor += texture(brightFilterSampler, coords[0]) * 0.0093;
    outColor += texture(brightFilterSampler, coords[1]) * 0.028002;
    outColor += texture(brightFilterSampler, coords[2]) * 0.065984;
    outColor += texture(brightFilterSampler, coords[3]) * 0.121703;
    outColor += texture(brightFilterSampler, coords[4]) * 0.175713;
    outColor += texture(brightFilterSampler, coords[5]) * 0.198596;
    outColor += texture(brightFilterSampler, coords[6]) * 0.175713;
    outColor += texture(brightFilterSampler, coords[7]) * 0.121703;
    outColor += texture(brightFilterSampler, coords[8]) * 0.065984;
    outColor += texture(brightFilterSampler, coords[9]) * 0.028002;
    outColor += texture(brightFilterSampler, coords[10]) * 0.0093;
    outColor.w = 1.0;
}