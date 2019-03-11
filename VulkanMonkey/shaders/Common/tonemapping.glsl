#ifndef TONEMAPPING_H_
#define TONEMAPPING_H_

vec3 SRGBtoLINEAR(vec3 srgbIn)
{
	vec3 bLess = step(vec3(0.04045),srgbIn);
	vec3 linOut = mix( srgbIn/vec3(12.92), pow((srgbIn+vec3(0.055))/vec3(1.055),vec3(2.4)), bLess );
	return linOut;
}

vec3 ToneMapReinhard(vec3 color, float exposure)
{
	return vec3(1.0) - exp(-color * exposure);
}

const float A = 0.15;
const float B = 0.50;
const float C = 0.10;
const float D = 0.20;
const float E = 0.02;
const float F = 0.30;
const float W = 11.2;
vec3 Uncharted2(vec3 x)
{
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

vec3 TonemapFilmic(vec3 color, float exposure)
{
	vec3 col = Uncharted2(color * exposure) * (1.0 / Uncharted2(vec3(W)));
	return pow(col, vec3(1.0f / 2.2f));
}

//== ACESFitted ===========================
//  Baking Lab
//  by MJP and David Neubelt
//  http://mynameismjp.wordpress.com/
//  All code licensed under the MIT license
//=========================================

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
const mat3 ACESInputMat =
{
	{0.59719, 0.07600, 0.02840},
	{0.35458, 0.90834, 0.13383},
	{0.04823, 0.01566, 0.83777}
};

// ODT_SAT => XYZ => D60_2_D65 => sRGB
const mat3 ACESOutputMat =
{
	{ 1.60475, -0.10208, -0.00327},
	{-0.53108,  1.10813, -0.07276},
	{-0.07367, -0.00605,  1.07602}
};

vec3 RRTAndODTFit(vec3 v)
{
	vec3 a = v * (v + vec3(0.0245786f)) - vec3(0.000090537f);
	vec3 b = v * (0.983729f * v + vec3(0.4329510f)) + vec3(0.238081f);
    return a / b;
}

vec3 ACESFitted(vec3 color)
{
    color = ACESInputMat * color;

    // Apply RRT and ODT
    color = RRTAndODTFit(color);

    color = ACESOutputMat * color;

    // Clamp to [0, 1]
    color = clamp(color, 0.0, 1.0);

    return color;
}

vec3 ACESFilm(vec3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}
#endif