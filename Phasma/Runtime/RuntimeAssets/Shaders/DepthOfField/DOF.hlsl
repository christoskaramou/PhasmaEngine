#ifndef DOF_H_
#define DOF_H_

#include "../Common/Common.hlsl"

// ----- Bokeh Depth of Field ----
static const float GOLDEN_ANGLE = 2.39996323;
static const float RAD_SCALE = 0.5;
static const float FAR_PLANE = 100.0;

// Sample a wide pixel average depth
float depth_average_wide(Texture2D depthTex, SamplerState sampler_depthTex, float2 texCoord, float pixel_range)
{
    float3 texDim = TextureSize(0, depthTex);
    float dx = pixel_range * texDim.x;
    float dy = pixel_range * texDim.y;

    float tl = depthTex.Sample(sampler_depthTex, texCoord + float2(-dx, -dy)).r;
    float tr = depthTex.Sample(sampler_depthTex, texCoord + float2(+dx, -dy)).r;
    float bl = depthTex.Sample(sampler_depthTex, texCoord + float2(-dx, +dy)).r;
    float br = depthTex.Sample(sampler_depthTex, texCoord + float2(+dx, +dy)).r;
    float ce = depthTex.Sample(sampler_depthTex, texCoord).r;

    return (tl + tr + bl + br + ce) * 0.2;
}

float getBlurSize(float depth, float center_depth, float focus_scale, float blur_range)
{
    float coc = abs(center_depth - depth) * focus_scale * FAR_PLANE;
    return clamp(coc, 0.0, blur_range);
}

float3 depthOfField(Texture2D colorTex, SamplerState sampler_colorTex, Texture2D depthTex, SamplerState sampler_depthTex, float2 tex_coord, float focus_scale, float blur_range)
{
    float center_depth = depth_average_wide(depthTex, sampler_depthTex, float2(0.5, 0.5), 20.0);
    float3 color = colorTex.Sample(sampler_colorTex, tex_coord).rgb;
    float tot = 1.0;

    float2 pixel_size = TexelSize(0, colorTex);

    float radius = RAD_SCALE;
    for (float ang = 0.0; radius < blur_range; ang += GOLDEN_ANGLE)
    {
        float2 tc = tex_coord + float2(cos(ang), sin(ang)) * pixel_size * radius;

        float3 sample_color = colorTex.Sample(sampler_colorTex, tc).rgb;
        float sample_depth = depthTex.Sample(sampler_depthTex, tc).r;
        float sample_size = getBlurSize(sample_depth, center_depth, focus_scale, blur_range);

        float m = smoothstep(radius-0.5, radius+0.5, sample_size);
        color += lerp(color / tot, sample_color, m);
        tot += 1.0;
        radius += RAD_SCALE / radius;
    }
    return color /= tot;
}

#endif
