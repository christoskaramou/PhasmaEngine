#include "Lighting.hlsl"

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    PS_OUTPUT_Color output;

    if (pc.passType && Transparency.Sample(sampler_Transparency, input.uv).x < 0.5f)
        discard;

    float depth = Depth.Sample(sampler_Depth, input.uv).x;

    // if the depth is near zero it hits the skybox (use epsilon for robustness)
    if (depth == 0.0)
    {
        // No need to re-sample skybox for transparent pass
        if (pc.passType)
            discard;

        // Skybox
        float3 wolrdPos  = GetPosFromUV(input.uv, depth, cb_invViewProj);
        float3 samplePos = normalize(wolrdPos - cb_camPos.xyz);
        output.color     = float4(Cube.Sample(sampler_Cube, samplePos).xyz, 1.0);

        return output;
    }

    float4 albedo    = Albedo.Sample(sampler_Albedo, input.uv);
    float3 normal    = normalize(Normal.Sample(sampler_Normal, input.uv).xyz * 2.0 - 1.0);
    float4 metRough  = MetRough.Sample(sampler_MetRough, input.uv);
    float3 emmission = Emission.Sample(sampler_Emission, input.uv).xyz;
    float3 wolrdPos  = GetPosFromUV(input.uv, depth, cb_invViewProj);

    Material material;
    material.albedo    = albedo.xyz;
    material.roughness = metRough.y;
    material.metallic  = metRough.z;
    material.F0        = lerp(0.04f, material.albedo, material.metallic);
    material.transmission = metRough.w;

    // Ambient
    float materialAO = metRough.x;
    float occlusion  = (cb_ssao ? Ssao.Sample(sampler_Ssao, input.uv).x : 1.0) * materialAO;
    float3 fragColor = 0.0;

    // Energy Compensation (Multi-Scattering)
    float3 V = normalize(cb_camPos.xyz - wolrdPos);
    float NdV = clamp(dot(normal, V), 0.001, 1.0);
    float2 envBRDF = LutIBL.Sample(sampler_LutIBL, float2(NdV, material.roughness)).xy;
    float E_spec = envBRDF.x + envBRDF.y;
    float3 energyCompensation = 1.0 + material.F0 * (1.0 / max(E_spec, 0.001) - 1.0);

    if (cb_shadows)
    {
        float shadow = CalculateShadows(wolrdPos, length(wolrdPos - cb_camPos.xyz), dot(normal, cb_sun.direction.xyz));
        fragColor += DirectLight(material, wolrdPos, cb_camPos.xyz, normal, occlusion, shadow, energyCompensation);
    }

    for (int i = 0; i < pc.num_point_lights; ++i)
        fragColor += ComputePointLight(i, material, wolrdPos, cb_camPos.xyz, normal, occlusion, energyCompensation);

    // Image Based Lighting
    if (cb_IBL)
    {
        float3 ibl = ImageBasedLighting(material, normal, V, Cube, sampler_Cube, envBRDF, occlusion);
        fragColor += ibl * cb_IBL_intensity;
    }

    // Add emmission
    output.color.rgb = fragColor + emmission;
    output.color.a   = albedo.a;

    return output;
}
