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

#ifndef COMMON_H_
#define COMMON_H_

#define PI 3.1415926535897932384626433832795
#define FLT_EPS 0.00000001
#define length2(x) dot(x, x)

// some hlsl to glsl mapping
#define saturate(x) clamp(x, 0.0, 1.0)
#define lerp(x, y, a) mix(x, y, a)
#define frac(x) fract(x)
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define int2 ivec2
#define int3 ivec3
#define int4 ivec4

bool IsSaturated(float x)    { return x == saturate(x); }
bool IsSaturated(vec2 x) { return IsSaturated(x.x) && IsSaturated(x.y); }

// [0.0, 1.0]
float PDnrand(vec2 n)
{
    return frac(sin(dot(n.xy, vec2(12.9898f, 78.233f)))* 43758.5453f);
}

// [-1.0, 1.0]
float PDsrand(vec2 n)
{
    return PDnrand(n) * 2.0 - 1.0;
}


// inverse_projection gives view space
// inverse_view_projection gives world space
vec3 GetPosFromUV(vec2 UV, float depth, mat4 mat)
{
    vec4 ndcPos;
    ndcPos.xy = UV * 2.0 - 1.0;
    ndcPos.z = depth;
    ndcPos.w = 1.0;

    vec4 clipPos = mat * ndcPos;
    return (clipPos / clipPos.w).xyz;
}

// Find the normal for this fragment, pulling either from a predefined normal map
// or from the interpolated mesh normal and tangent attributes.
vec3 GetNormal(vec3 positionWS, sampler2D normalMap, vec3 inNormal, vec2 inUV)
{
    // Perturb normal, see http://www.thetenthplanet.de/archives/1180
    vec3 tangentNormal = texture(normalMap, inUV).xyz * 2.0f - 1.0f;

    vec3 q1 = dFdx(positionWS);
    vec3 q2 = dFdy(positionWS);
    vec2 st1 = dFdx(inUV);
    vec2 st2 = dFdy(inUV);

    vec3 N = normalize(inNormal);
    vec3 T = normalize(q1 * st2.t - q2 * st1.t);
    vec3 B = normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    return normalize(TBN * tangentNormal);
}

// Return average velocity
vec3 DilateAverage(sampler2D samplerVelocity, vec2 texCoord)
{
    ivec2 texDim = textureSize(samplerVelocity, 0);
    float dx = 2.0f * float(texDim.x);
    float dy = 2.0f * float(texDim.y);

    vec3 tl = texture(samplerVelocity, texCoord + vec2(-dx, -dy)).xyz;
    vec3 tr = texture(samplerVelocity, texCoord + vec2(+dx, -dy)).xyz;
    vec3 bl = texture(samplerVelocity, texCoord + vec2(-dx, +dy)).xyz;
    vec3 br = texture(samplerVelocity, texCoord + vec2(+dx, +dy)).xyz;
    vec3 ce = texture(samplerVelocity, texCoord).xyz;

    return (tl + tr + bl + br + ce) * 0.2;
}

// Returns velocity with closest depth
vec3 DilateDepth3X3(sampler2D samplerVelocity, sampler2D samplerDepth, vec2 texCoord)
{
    ivec2 texDim = textureSize(samplerDepth, 0);
    vec2 texelSize = 1.0 / vec2(float(texDim.x), float(texDim.y));
    float closestDepth = 0.0;
    vec2 closestTexCoord = texCoord;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 offset = vec2(x, y) * texelSize;
            float depth = texture(samplerDepth, texCoord + offset).r;
            if (depth > closestDepth)// using ">" because depth is reversed
            {
                closestTexCoord    = texCoord + offset;
            }
        }
    }
    return texture(samplerVelocity, closestTexCoord).xyz;
}

float Luminance(vec3 color)
{
    //vec3 PALandNTSC = vec3(0.299, 0.587, 0.114);
    //vec3 HDTV = vec3(0.2126, 0.7152, 0.0722);
    vec3 HDR = vec3(0.2627, 0.6780, 0.0593);

    return dot(color, HDR);
}

vec3 SharpenSimple(sampler2D tex, vec2 UV)
{
    ivec2 texDim = textureSize(tex, 0);
    vec2 texelSize = 1.0 / vec2(float(texDim.x), float(texDim.y));

    vec3 sum = vec3(0.0);

    sum += -1. * texture(tex, UV + vec2(0.0, -1.0) * texelSize).xyz;
    sum += -1. * texture(tex, UV + vec2(1.0, 0.0) * texelSize).xyz;
    sum += +5. * texture(tex, UV + vec2(0.0, 0.0) * texelSize).xyz;
    sum += -1. * texture(tex, UV + vec2(0.0, 1.0) * texelSize).xyz;
    sum += -1. * texture(tex, UV + vec2(-1.0, 0.0) * texelSize).xyz;

    return sum;
}

vec3 SharpenLuma(sampler2D tex, vec2 UV, float sharp_strength, float sharp_clamp, float offset_bias)
{
    /*
        LumaSharpen 1.4.1
        original hlsl by Christian Cann Schuldt Jensen ~ CeeJay.dk
        port to glsl by Anon
        ported back to hlsl and modified by Panos karabelas
        It blurs the original pixel with the surrounding pixels and then subtracts this blur to sharpen the image.
        It does this in luma to avoid color artifacts and allows limiting the maximum sharpning to avoid or lessen halo artifacts.
        This is similar to using Unsharp Mask in Photoshop.
    */

    // -- Sharpening --
    //#define sharp_strength 1.0f   //[0.10 to 3.00] Strength of the sharpening
    //#define sharp_clamp    0.35f  //[0.000 to 1.000] Limits maximum amount of sharpening a pixel receives - Default is 0.035

    // -- Advanced sharpening settings --
    //#define offset_bias 1.0f  //[0.0 to 6.0] Offset bias adjusts the radius of the sampling pattern.
    #define CoefLuma vec3(0.2126f, 0.7152f, 0.0722f)// BT.709 & sRBG luma coefficient (Monitors and HD Television)

    vec4 colorInput = texture(tex, UV);
    vec3 ori = colorInput.rgb;

    // -- Combining the strength and luma multipliers --
    vec3 sharp_strength_luma = (CoefLuma * sharp_strength);//I'll be combining even more multipliers with it later on

    // -- Gaussian filter --
    //   [ .25, .50, .25]     [ 1 , 2 , 1 ]
    //   [ .50,   1, .50]  =  [ 2 , 4 , 2 ]
    //   [ .25, .50, .25]     [ 1 , 2 , 1 ]

    ivec2 texDim = textureSize(tex, 0);
    vec2 pixelSize = (1.0 / vec2(float(texDim.x), float(texDim.y)));

    float px = pixelSize.x;
    float py = pixelSize.y;

    vec3 blur_ori = texture(tex, UV + vec2(+px, -py) * 0.5f * offset_bias).rgb;// South East
    blur_ori +=        texture(tex, UV + vec2(-px, -py) * 0.5f * offset_bias).rgb;// South West
    blur_ori +=        texture(tex, UV + vec2(+px, +py) * 0.5f * offset_bias).rgb;// North East
    blur_ori +=        texture(tex, UV + vec2(-px, +py) * 0.5f * offset_bias).rgb;// North West
    blur_ori *= 0.25f;// ( /= 4) Divide by the number of texture fetches

    // -- Calculate the sharpening --
    vec3 sharp = ori - blur_ori;//Subtracting the blurred image from the original image

    // -- Adjust strength of the sharpening and clamp it--
    vec4 sharp_strength_luma_clamp = vec4(sharp_strength_luma * (0.5f / sharp_clamp), 0.5f);//Roll part of the clamp into the dot

    float sharp_luma = clamp((dot(vec4(sharp, 1.0f), sharp_strength_luma_clamp)), 0.0f, 1.0f);//Calculate the luma, adjust the strength, scale up and clamp
    sharp_luma = (sharp_clamp * 2.0f) * sharp_luma - sharp_clamp;//scale down

    // -- Combining the values to get the final sharpened pixel	--
    colorInput.rgb = colorInput.rgb + sharp_luma;// Add the sharpening to the input color.
    return clamp(colorInput, 0.0f, 1.0f).rgb;
}

float MipMapLevel(vec2 texture_coordinate)// in texel units
{
    vec2  dxVtc        = dFdx(texture_coordinate);
    vec2  dyVtc        = dFdy(texture_coordinate);
    float delta_max_sqr = max(dot(dxVtc, dxVtc), dot(dyVtc, dyVtc));
    float mml = 0.5 * log2(delta_max_sqr);
    return max(0, mml);
}

#endif
