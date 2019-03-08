#ifndef TONEMAPPING_H_
#define TONEMAPPING_H_

vec3 ToneMapReinhard(vec3 color, float exposure)
{
	return vec3(1.0) - exp(-color * exposure);
	//return color / (vec3(1.0) + color);
}

vec3 Uncharted2(vec3 x)
{
    float A = 0.15;
	float B = 0.50;
	float C = 0.10;
	float D = 0.20;
	float E = 0.02;
	float F = 0.30;
	float W = 11.2;
    return (((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F);
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
    {0.59719, 0.35458, 0.04823},
    {0.07600, 0.90834, 0.01566},
    {0.02840, 0.13383, 0.83777}
};

// ODT_SAT => XYZ => D60_2_D65 => sRGB
const mat3 ACESOutputMat =
{
    { 1.60475, -0.53108, -0.07367},
    {-0.10208,  1.10813, -0.00605},
    {-0.00327, -0.07276,  1.07602}
};

vec3 RRTAndODTFit(vec3 v)
{
	vec3 a = v * (v + vec3(0.0245786f)) - vec3(0.000090537f);
	vec3 b = v * (0.983729f * v + vec3(0.4329510f)) + vec3(0.238081f);
    return a / b;
}

vec3 ACESFitted(vec3 color)
{
    color = transpose(ACESInputMat) * color;

    // Apply RRT and ODT
    color = RRTAndODTFit(color);

    color = transpose(ACESOutputMat) * color;

    // Clamp to [0, 1]
    color = clamp(color, 0.0, 1.0);

    return color;
}

//vec3 ToneMap(vec3 color)
//{
//    if (g_toneMapping == 0) // OFF
//    {
//		// Do nothing
//    }
//	else if (g_toneMapping == 1) // ACES
//	{
//		color = ACESFitted(color);
//	}
//	else if (g_toneMapping == 2) // REINHARD
//	{
//		color = ToneMapReinhard(color);
//	}
//	else if (g_toneMapping == 3) // UNCHARTED 2
//	{
//		color = Uncharted2(color);
//	}
//	
//	return color;
//}

#endif