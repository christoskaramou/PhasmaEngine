#ifndef COMMON_H_
#define COMMON_H_

// Defines
#define PI 3.1415926535897932384626433832795
#define FLT_EPS 0.00000001
#define length2(x) dot(x, x)

#define TexSamplerDecl(bind, set, tex) \
[[vk::combinedImageSampler]][[vk::binding(bind, set)]] \
Texture2D tex; \
[[vk::combinedImageSampler]][[vk::binding(bind, set)]] \
SamplerState sampler_##tex;

#define CubeSamplerDecl(bind, set, tex) \
[[vk::combinedImageSampler]][[vk::binding(bind, set)]] \
TextureCube tex; \
[[vk::combinedImageSampler]][[vk::binding(bind, set)]] \
SamplerState sampler_##tex;

bool IsSaturated(float x) { return x == saturate(x); }
bool IsSaturated(float2 x) { return IsSaturated(x.x) && IsSaturated(x.y); }

// [0.0, 1.0]
float PDnrand(float2 n)
{
    return frac(sin(dot(n.xy, float2(12.9898f, 78.233f)))* 43758.5453f);
}

// [-1.0, 1.0]
float PDsrand(float2 n)
{
    return PDnrand(n) * 2.0 - 1.0;
}


// inverse_projection gives view space
// inverse_view_projection gives world space
float3 GetPosFromUV(float2 UV, float depth, float4x4 mat)
{
    float4 ndcPos;
    ndcPos.xy = UV * 2.0 - 1.0;
    ndcPos.z = depth;
    ndcPos.w = 1.0;

    float4 clipPos = mul(ndcPos, mat);
    return (clipPos / clipPos.w).xyz;
}

// Find the normal for this fragment, pulling either from a predefined normal map
// or from the interpolated mesh normal and tangent attributes.
float3 GetNormal(float3 positionWS, float3 tangentNormal, float3 inNormal, float2 inUV)
{
    // Perturb normal, see http://www.thetenthplanet.de/archives/1180
    float3 tNormal = tangentNormal * 2.0f - 1.0f;

    float3 q1 = ddx(positionWS);
    float3 q2 = ddy(positionWS);
    float2 st1 = ddx(inUV);
    float2 st2 = ddy(inUV);

    float3 N = normalize(inNormal);
    float3 T = normalize(q1 * st2.y - q2 * st1.y);
    float3 B = normalize(cross(N, T));
    float3x3 TBN = float3x3(T, B, N);

    return normalize(mul(tNormal, TBN));
}

float3 TextureSize(uint mip, Texture2D tex)
{
    float width;
    float height;
    float numMips;
    tex.GetDimensions(mip, width, height, numMips);
    return float3(width, height, numMips);
}

float3 TextureSize(uint mip, TextureCube tex)
{
    float width;
    float height;
    float numMips;
    tex.GetDimensions(mip, width, height, numMips);
    return float3(width, height, numMips);
}

float2 TexelSize(uint mip, Texture2D tex)
{
    return 1.0f / TextureSize(mip, tex).xy;
}

float2 TexelSize(uint mip, TextureCube tex)
{
    return 1.0f / TextureSize(mip, tex).xy;
}

// Returns the closest depth in a 3X3 pixel grid
float DilateDepth3X3_UV(Texture2D tex, SamplerState sampler_tex, float2 uv, out float2 outUv)
{
    float outDepth = 0.0; // TODO: have a flag to check if depth is reversed
    outUv = uv;

    float2 texelSize = TexelSize(0, tex);

    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y) * texelSize;
            float depth = tex.Sample(sampler_tex, uv + offset).r;
            if (depth > outDepth)
            {
                outDepth = depth;
                outUv = uv + offset;
            }
        }
    }

    return outDepth;
}

float Luminance(float3 color)
{
    //float3 avg = float3(0.299, 0.587, 0.114); // PALandNTSC
    //float3 avg = float3(0.2126, 0.7152, 0.0722); // HDTV
    float3 avg = float3(0.2627, 0.6780, 0.0593); // HDR

    return dot(color, avg);
}

float3 SharpenSimple(Texture2D tex, SamplerState sampler_tex, float2 UV)
{
    float2 texelSize = TexelSize(0, tex);

    float3 sum = 0.0f;

    sum += -1. * tex.Sample(sampler_tex, UV + float2(0.0, -1.0) * texelSize).xyz;
    sum += -1. * tex.Sample(sampler_tex, UV + float2(1.0, 0.0) * texelSize).xyz;
    sum += +5. * tex.Sample(sampler_tex, UV + float2(0.0, 0.0) * texelSize).xyz;
    sum += -1. * tex.Sample(sampler_tex, UV + float2(0.0, 1.0) * texelSize).xyz;
    sum += -1. * tex.Sample(sampler_tex, UV + float2(-1.0, 0.0) * texelSize).xyz;

    return sum;
}

float3 SharpenLuma(Texture2D tex, SamplerState sampler_tex, float2 UV, float sharp_strength, float sharp_clamp, float offset_bias)
{
    // -- Sharpening --
    //#define sharp_strength 1.0f   //[0.10 to 3.00] Strength of the sharpening
    //#define sharp_clamp    0.35f  //[0.000 to 1.000] Limits maximum amount of sharpening a pixel receives - Default is 0.035

    // -- Advanced sharpening settings --
    //#define offset_bias 1.0f  //[0.0 to 6.0] Offset bias adjusts the radius of the sampling pattern.
    #define CoefLuma float3(0.2126f, 0.7152f, 0.0722f)// BT.709 & sRBG luma coefficient (Monitors and HD Television)

    float4 colorInput = tex.Sample(sampler_tex, UV);
    float3 ori = colorInput.rgb;

    // -- Combining the strength and luma multipliers --
    float3 sharp_strength_luma = (CoefLuma * sharp_strength);//I'll be combining even more multipliers with it later on

    // -- Gaussian filter --
    //   [ .25, .50, .25]     [ 1 , 2 , 1 ]
    //   [ .50,   1, .50]  =  [ 2 , 4 , 2 ]
    //   [ .25, .50, .25]     [ 1 , 2 , 1 ]

    float2 texelSize = TexelSize(0, tex);
    float tx = texelSize.x;
    float ty = texelSize.y;

    float3 blur_ori =   tex.Sample(sampler_tex, UV + float2(+tx, -ty) * 0.5f * offset_bias).rgb;// South East
    blur_ori +=         tex.Sample(sampler_tex, UV + float2(-tx, -ty) * 0.5f * offset_bias).rgb;// South West
    blur_ori +=         tex.Sample(sampler_tex, UV + float2(+tx, +ty) * 0.5f * offset_bias).rgb;// North East
    blur_ori +=         tex.Sample(sampler_tex, UV + float2(-tx, +ty) * 0.5f * offset_bias).rgb;// North West
    blur_ori *=         0.25f;// ( /= 4) Divide by the number of texture fetches

    // -- Calculate the sharpening --
    float3 sharp = ori - blur_ori;//Subtracting the blurred image from the original image

    // -- Adjust strength of the sharpening and clamp it--
    float4 sharp_strength_luma_clamp = float4(sharp_strength_luma * (0.5f / sharp_clamp), 0.5f);//Roll part of the clamp into the dot

    float sharp_luma = saturate((dot(float4(sharp, 1.0f), sharp_strength_luma_clamp)));//Calculate the luma, adjust the strength, scale up and clamp
    sharp_luma = (sharp_clamp * 2.0f) * sharp_luma - sharp_clamp;//scale down

    // -- Combining the values to get the final sharpened pixel	--
    colorInput.rgb = colorInput.rgb + sharp_luma;// Add the sharpening to the input color.
    return saturate(colorInput).rgb;
}

float MipMapLevel(float2 texture_coordinate)// in texel units
{
    float2 dxVtc = ddx(texture_coordinate);
    float2 dyVtc = ddy(texture_coordinate);
    float delta_max_sqr = max(dot(dxVtc, dxVtc), dot(dyVtc, dyVtc));
    float mml = 0.5 * log2(delta_max_sqr);
    return max(0.f, mml);
}

#endif
