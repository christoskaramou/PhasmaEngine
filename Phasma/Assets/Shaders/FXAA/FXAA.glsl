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

#ifndef FXAA_H_
#define FXAA_H_

const float RDC_MIN = 0.015625f;// 1.0 / 64.0;
const float RDC = 0.25f;// 1.0 / 4.0
const float RANGE = 4.0f;

vec3 FXAA(sampler2D uInput, vec2 vUV)
{
    ivec2 texDim = textureSize(uInput, 0);
    vec2 texelSize = 1.0 / vec2(float(texDim.x), float(texDim.y));

    vec3 rgbNW = textureOffset(uInput, vUV, ivec2(-1, -1)).rgb;
    vec3 rgbNE = textureOffset(uInput, vUV, ivec2(+1, -1)).rgb;
    vec3 rgbSW = textureOffset(uInput, vUV, ivec2(-1, +1)).rgb;
    vec3 rgbSE = textureOffset(uInput, vUV, ivec2(+1, +1)).rgb;

    vec3 texColor = texture(uInput, vUV).rgb;
    const vec3 luma = vec3(0.2627, 0.678, 0.0593);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM = dot(texColor, luma);
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) *
    (0.25 * RDC), RDC_MIN);

    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, -RANGE, RANGE) * texelSize;

    vec3 rgbA = 0.5 * (
    texture(uInput, vUV + dir * (1.0 / 3.0 - 0.5)).rgb +
    texture(uInput, vUV + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
    texture(uInput, vUV + dir * -0.5).rgb +
    texture(uInput, vUV + dir * 0.5).rgb);

    float lumaB = dot(rgbB, luma);
    vec3 color;
    if ((lumaB < lumaMin) || (lumaB > lumaMax))
    color = rgbA;
    else
    color = rgbB;

    return color;
}

#endif
