#include "Light.hlsl"

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    PS_OUTPUT_Color output;

    if (pc.transparentPass && Transparency.Sample(sampler_Transparency, input.uv).x < 0.5f)
        discard;

    float depth = Depth.Sample(sampler_Depth, input.uv).x;

    // if the depth is maximum it hits the skybox
    if (depth == 0.0)
    {
        // Skybox
        float3 wolrdPos  = GetPosFromUV(input.uv, depth, cb_invViewProj);
        float3 samplePos = normalize(wolrdPos - cb_camPos.xyz);
        output.color     = float4(Cube.Sample(sampler_Cube, samplePos).xyz, 1.0);

        return output;
    }

    float4 albedo    = Albedo.Sample(sampler_Albedo, input.uv);
    float3 normal    = Normal.Sample(sampler_Normal, input.uv).xyz * 2.0 - 1.0;
    float3 metRough  = MetRough.Sample(sampler_MetRough, input.uv).xyz;
    float3 emmission = Emission.Sample(sampler_Emission, input.uv).xyz;
    float3 wolrdPos  = GetPosFromUV(input.uv, depth, cb_invViewProj);

    Material material;
    material.albedo    = albedo.xyz;
    material.roughness = metRough.y;
    material.metallic  = metRough.z;
    material.F0        = lerp(0.04f, material.albedo, material.metallic);

    // Ambient
    float occlusion  = cb_ssao ? Ssao.Sample(sampler_Ssao, input.uv).x : 1.0;
    float3 fragColor = 0.0;

    if (cb_shadows)
    {
        float shadow = CalculateShadows(wolrdPos, length(wolrdPos - cb_camPos.xyz), dot(normal, cb_sun.direction.xyz));
        fragColor    += DirectLight(material, wolrdPos, cb_camPos.xyz, normal, occlusion, shadow);
    }

    for (int i = 0; i < pc.num_point_lights; ++i)
        fragColor += ComputePointLight(i, material, wolrdPos, cb_camPos.xyz, normal, occlusion);

    // Image Based Lighting
    if (cb_IBL)
    {
        IBL ibl = ImageBasedLighting(material, normal, normalize(wolrdPos - cb_camPos.xyz), Cube, sampler_Cube, LutIBL, sampler_LutIBL);
        fragColor += ibl.final_color * cb_IBL_intensity;
    }

    // Add emmission
    output.color.rgb = fragColor + emmission;
    output.color.a   = albedo.a;

    return output;
}
