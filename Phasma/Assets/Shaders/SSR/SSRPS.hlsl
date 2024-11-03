#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

// Sampler Declarations
TexSamplerDecl(0, 0, Color)
TexSamplerDecl(1, 0, Depth)
TexSamplerDecl(2, 0, Normal)
TexSamplerDecl(3, 0, MetallicRoughness)
TexSamplerDecl(4, 0, Albedo)

// Camera Buffer
[[vk::binding(5, 0)]] cbuffer Camera
{
    float4 cb_camPos;
    float4 cb_camFront;
    float4 cb_size;
    float4 cb_dummy1;
    float4x4 cb_projection;
    float4x4 cb_view;
    float4x4 cb_invProj;
}

// Constants for better readability and tweaking
static const float SSR_DELTA_THRESHOLD = 0.001f;
static const int SSR_MAX_LOOPS = 30;
static const int SSR_MIN_LOOPS = 5;
static const float SSR_ANGLE_THRESHOLD = 0.1;

float2 GetUVFromViewPos(float3 view_position)
{
    float4 uv = mul(float4(view_position, 1.0), cb_projection);
    return (uv.xy / uv.w) * 0.5f + 0.5f;
}

// Fresnel-Schlick approximation
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0 - cosTheta, 5.0f);
}

// Screen space reflections with improvements
float3 ScreenSpaceReflections(float3 position, float3 normal, float metallic, float roughness, float3 albedo)
{
    float3 reflection = reflect(normalize(position), normalize(normal));
    float VdotR       = max(0.0, dot(normalize(position), normalize(reflection)));
    float3 F0         = lerp(0.04f, albedo, metallic);
    float3 fresnel    = FresnelSchlick(VdotR, F0) * (1.0f - roughness);
    float3 step       = reflection;
    float3 new_pos    = position + step;

    float angleFactor = 1.0 - abs(VdotR); // Closer to 1 means the vectors are more perpendicular
    int loops = int(lerp(float(SSR_MAX_LOOPS), float(SSR_MIN_LOOPS), angleFactor));

    if(abs(VdotR) < SSR_ANGLE_THRESHOLD)
    {
        return 0.0;
    }

    // Binary search for the closest point on the surface
    for (int i = 0; i < loops; i++)
    {
        float2 uv = GetUVFromViewPos(new_pos);

        // UV boundaries
        if (any(uv < 0.0f) || any(uv > 1.0f))
        {
            step *= 0.5;
            new_pos -= step + 0.000001;
            continue;
        }

        float depth_sample       = Depth.Sample(sampler_Depth, uv).x;
        float actual_view_depth  = abs(GetPosFromUV(uv, depth_sample, cb_invProj).z);
        float curr_view_depth    = abs(new_pos.z);
        float delta              = curr_view_depth - actual_view_depth;
        float adaptive_threshold = SSR_DELTA_THRESHOLD * (1.0 + curr_view_depth);

        if (abs(delta) < 0.01f)
        {
            float3 color = Color.Sample(sampler_Color, uv).xyz;
            float2 fade  = 1.0f - abs(uv * 2.0f - 1.0f);
            return color * fresnel * min(fade.x, fade.y);
        }

        // If delta is positive, we moved through the surface, so we need to reduce the step and invert the direction
        if (delta > 0.0f)
        {
            step *= 0.5f;
            new_pos -= step + 0.000001;
        }
        else
        {
            new_pos += step + 0.000001; // keep going
        }
    }

    return 0.0;
}

// Main Pixel Shader
PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    PS_OUTPUT_Color output;

    float3 albedo            = Albedo.Sample(sampler_Albedo, input.uv).xyz;
    float4 color             = Color.Sample(sampler_Color, input.uv);
    float depth              = Depth.Sample(sampler_Depth, input.uv).x;
    float3 position          = GetPosFromUV(input.uv, depth, cb_invProj);
    float4 tangent_normal    = float4(Normal.Sample(sampler_Normal, input.uv).xyz * 2.0 - 1.0, 0.0);
    float3 normal            = normalize(mul(tangent_normal, cb_view).xyz);
    float4 metallicRoughness = MetallicRoughness.Sample(sampler_MetallicRoughness, input.uv);
    float metallic           = metallicRoughness.g;
    float roughness          = metallicRoughness.b;

    float3 ssr = ScreenSpaceReflections(position, normal, metallic, roughness, albedo);
    output.color = color + float4(ssr, 1.0);

    return output;
}
