#ifndef TONEMAPPING_H_
#define TONEMAPPING_H_

float3 SRGBtoLINEAR(float3 srgbIn)
{
    float3 bLess = step(0.04045, srgbIn);
    float3 linOut = lerp(srgbIn/12.92, pow((srgbIn+0.055)/1.055, 2.4), bLess);
    return linOut;
}

float3 Reinhard(float3 hdr)
{
    float k = 1.0;
    return hdr / (hdr + k);
}

float3 ReinhardInverse(float3 sdr)
{
    float k = 1.0;
    return k * sdr / (k - sdr);
}

float3 ToneMapReinhard(float3 color, float exposure)
{
    return 1.0 - exp(-color * exposure);
}

static const float A = 0.15;
static const float B = 0.50;
static const float C = 0.10;
static const float D = 0.20;
static const float E = 0.02;
static const float F = 0.30;
static const float W = 11.2;
float3 Uncharted2(float3 x)
{
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

float3 TonemapFilmic(float3 color, float exposure)
{
    float3 col = Uncharted2(color * exposure) * (1.0 / Uncharted2(W));
    return pow(col, 1.0f / 2.2f);
}

//== ACESFitted ===========================
//  Baking Lab
//  by MJP and David Neubelt
//  http://mynameismjp.wordpress.com/
//  All code licensed under the MIT license
//=========================================

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
static const float3x3 ACESInputMat =
{
{ 0.59719, 0.07600, 0.02840 },
{ 0.35458, 0.90834, 0.13383 },
{ 0.04823, 0.01566, 0.83777 }
};

// ODT_SAT => XYZ => D60_2_D65 => sRGB
static const float3x3 ACESOutputMat =
{
{ 1.60475, -0.10208, -0.00327 },
{ -0.53108, 1.10813, -0.07276 },
{ -0.07367, -0.00605, 1.07602 }
};

float3 RRTAndODTFit(float3 v)
{
    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

float3 ACESFitted(float3 color)
{
    color = mul(color, ACESInputMat);

    // Apply RRT and ODT
    color = RRTAndODTFit(color);

    color = mul(color, ACESOutputMat);

    // Clamp to [0, 1]
    color = clamp(color, 0.0, 1.0);

    return color;
}

float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

#endif
