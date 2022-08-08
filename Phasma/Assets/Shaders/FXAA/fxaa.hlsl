#ifndef FXAA_H_
#define FXAA_H_

#include "../Common/common.hlsl"

static const float RDC_MIN = 0.015625f; // 1.0 / 64.0;
static const float RDC = 0.25f; // 1.0 / 4.0
static const float RANGE = 4.0f;

float3 FXAA(Texture2D uInput, SamplerState sampler_uInput, float2 vUV)
{
    float2 texelSize = TexelSize(0, uInput);

    float3 rgbNW = uInput.Sample(sampler_uInput, vUV, int2(-1, -1)).rgb;
    float3 rgbNE = uInput.Sample(sampler_uInput, vUV, int2(+1, -1)).rgb;
    float3 rgbSW = uInput.Sample(sampler_uInput, vUV, int2(-1, +1)).rgb;
    float3 rgbSE = uInput.Sample(sampler_uInput, vUV, int2(+1, +1)).rgb;

    float3 texColor = uInput.Sample(sampler_uInput, vUV).rgb;
    const float3 luma = float3(0.2627, 0.678, 0.0593);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM = dot(texColor, luma);
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * RDC), RDC_MIN);

    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, -RANGE, RANGE) * texelSize;

    float3 rgbA = 0.5 * (
        uInput.Sample(sampler_uInput, vUV + dir * (1.0 / 3.0 - 0.5)).rgb +
        uInput.Sample(sampler_uInput, vUV + dir * (2.0 / 3.0 - 0.5)).rgb);
    float3 rgbB = rgbA * 0.5 + 0.25 * (
        uInput.Sample(sampler_uInput, vUV + dir * -0.5).rgb +
        uInput.Sample(sampler_uInput, vUV + dir * 0.5).rgb);

    float lumaB = dot(rgbB, luma);
    float3 color;
    if ((lumaB < lumaMin) || (lumaB > lumaMax))
        color = rgbA;
    else
        color = rgbB;

    return color;
}

#endif
