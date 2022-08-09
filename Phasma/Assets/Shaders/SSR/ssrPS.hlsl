#include "../Common/common.hlsl"

TexSamplerDecl(0, 0, Albedo)
TexSamplerDecl(1, 0, Depth)
TexSamplerDecl(2, 0, Normal)

[[vk::binding(3, 0)]] cbuffer Camera
{
    float4 cb_camPos;
    float4 cb_camFront;
    float4 cb_size;
    float4 cb_dummy1;
    float4x4 cb_projection;
    float4x4 cb_view;
    float4x4 cb_invProj;
};


struct PS_INPUT {
    float2 uv : TEXCOORD0;
};
struct PS_OUTPUT {
    float4 color : SV_Target0;
};

float3 ScreenSpaceReflections(float3 position, float3 normal);

PS_OUTPUT mainPS(PS_INPUT input)
{
    PS_OUTPUT output;

    float depth = Depth.Sample(sampler_Depth, input.uv).x;
    float3 position = GetPosFromUV(input.uv, depth, cb_invProj);
    float4 tangent_normal = float4(Normal.Sample(sampler_Normal, input.uv).xyz * 2.0 - 1.0, 0.0);
    float3 normal = normalize(mul(tangent_normal, cb_view).xyz); 

    output.color = float4(ScreenSpaceReflections(position, normal), 1.0);

    return output;
}

// Screen space reflections
float3 ScreenSpaceReflections(float3 position, float3 normal)
{
    float3 reflection = reflect(position, normal);

    float VdotR = max(dot(normalize(position), normalize(reflection)), 0.0);
    float fresnel = pow(VdotR, 5); // small hack, not fresnel at all

    float3 step = reflection;
    float3 new_pos = position + step;

    float loops = max(sign(VdotR), 0.0) * 30;
    for (int i = 0; i < loops; i++)
    {
        float4 new_view_pos = float4(new_pos, 1.0);
        float4 uv_proj = mul(new_view_pos, cb_projection);
        uv_proj.xy = (uv_proj.xy / uv_proj.w) * 0.5 + 0.5;

        float2 check_uv = max(sign(uv_proj.xy * (1.0 - uv_proj.xy)), 0.0);
        if (check_uv.x * check_uv.y < 1.0){
            step *= 0.5;
            new_pos -= step;
            continue;
        }

        float curr_view_depth = abs(new_view_pos.z);
        float depth = Depth.Sample(sampler_Depth, uv_proj.xy).x;
        float view_depth = abs(GetPosFromUV(uv_proj.xy, depth, cb_invProj).z);

        float delta = abs(curr_view_depth - view_depth);
        if (delta < 0.01f)
        {
            float2 fade = uv_proj.xy * 2.0 - 1.0;
            fade = abs(fade);
            float fade_amount = min(1.0 - fade.x, 1.0 - fade.y);

            return Albedo.Sample(sampler_Albedo, uv_proj.xy).xyz * fresnel * fade_amount;
        }

        step *= 1.0 - 0.5 * max(sign(curr_view_depth - view_depth), 0.0);
        new_pos += step * (sign(view_depth - curr_view_depth) + 0.000001);
    }

    return 0.0;
}
