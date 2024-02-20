#include "Light.hlsl"

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    PS_OUTPUT_Color output;

    float depth = Depth.Sample(sampler_Depth, input.uv).x;

    // if the depth is maximum it hits the skybox
    if (depth == 0.0)
    {
        // Skybox
        float3 wolrdPos  = GetPosFromUV(input.uv, depth, cb_invViewProj);
        float3 samplePos = normalize(wolrdPos - cb_camPos.xyz);
        output.color     = float4(Cube.Sample(sampler_Cube, samplePos).xyz, 1.0);

        // Fog
        // cb_effects3.y -> fog on/off
        // cb_effects2.w -> fog max height
        // cb_effects2.x -> fog global thickness
        // cb_effects3.x -> fog ground thickness
        if (cb_effects3.y > 0.5)
        {
            float fogNear           = 0.5;
            float fogFar            = 1000.0;
            float depth             = length(wolrdPos - cb_camPos.xyz);
            float groundHeightMin   = -100.0;
            float groundHeightMax   = cb_effects2.w * 20.0;
            float globalThickness   = 1.0;
            float groundThickness   = 2.0;
            float depthFactor       = clamp(1.0 - (fogFar - depth) / (fogFar - fogNear), 0.0, 1.0);
            float heightFactor      = clamp(1.0 - (wolrdPos.y - groundHeightMin) / (groundHeightMax - groundHeightMin), 0.0, 1.0);
            float globalFactor      = depthFactor * globalThickness;
            float noiseFactor       = 1.0;// texture(NoiseMap, position.xz).x;
            float groundFactor      = heightFactor * depthFactor * noiseFactor * groundThickness;
            float fogFactor         = clamp(groundFactor + globalFactor, 0.0, 1.0);
            output.color.xyz        = lerp(output.color.xyz, 1.0, fogFactor);

            // Volumetric light
            if (cb_effects1.y > 0.5)
                output.color.xyz += VolumetricLighting(cb_sun, wolrdPos, input.uv, cb_cascades[0], fogFactor, pc.frameBufferSize);
        }

        return output;
    }

    float3 wolrdPos     = GetPosFromUV(input.uv, depth, cb_invViewProj);
    float3 normal       = Normal.Sample(sampler_Normal, input.uv).xyz * 2.0 - 1.0;
    float3 metRough     = MetRough.Sample(sampler_MetRough, input.uv).xyz;
    float4 albedo       = Albedo.Sample(sampler_Albedo, input.uv);
    float3 emmission    = Emission.Sample(sampler_Emission, input.uv).xyz;

    Material material;
    material.albedo     = albedo.xyz;
    material.roughness  = metRough.y;
    material.metallic   = metRough.z;
    material.F0         = lerp(0.04f, material.albedo, material.metallic);

    // Ambient
    float occlusion     = cb_effects0.x > 0.5 ? SsaoBlur.Sample(sampler_SsaoBlur, input.uv).x : 1.0;
    float skyLight      = clamp(cb_sun.color.a, 0.025f, 1.0f);
    skyLight            *= cb_effects3.z > 0.5 ? 0.25 : 0.15;
    float ambientLight  = skyLight * occlusion;
    float3 fragColor    = 0.0;

    // cb_effects3.z -> shadow cast
    if (cb_effects3.z > 0.5)
    {
        float shadow    = CalculateShadows(wolrdPos, length(wolrdPos - cb_camPos.xyz), dot(normal, cb_sun.direction.xyz));
        fragColor       += DirectLight(material, wolrdPos, cb_camPos.xyz, normal, occlusion, shadow);
    }

    // IBL
    IBL ibl;
    ibl.reflectivity = 0.5;
    if (cb_effects1.x > 0.5) {
        ibl         = ImageBasedLighting(material, normal, normalize(wolrdPos - cb_camPos.xyz), Cube, sampler_Cube, LutIBL, sampler_LutIBL);
        fragColor   += ibl.final_color * ambientLight;
    }

    for (int i = 0; i < MAX_POINT_LIGHTS; ++i)
        fragColor += ComputePointLight(i, material, wolrdPos, cb_camPos.xyz, normal, occlusion);

    // Add emmission
    output.color.rgb    = fragColor + emmission;
    output.color.a      = albedo.a;

    // // SSR
    // if (cb_effects0.y > 0.5)
    // {
    //     float3 ssr      = Ssr.Sample(sampler_Ssr, input.uv).xyz * ibl.reflectivity;
    //     output.color    += float4(ssr, 0.0);// * (1.0 - material.roughness);
    // }

    // Tone Mapping
    if (cb_effects0.z > 0.5)
        output.color.xyz = Reinhard(output.color.xyz);

    // Fog
    if (cb_effects3.y > 0.5)
    {
        // cb_effects3.y -> fog on/off
        // cb_effects2.w -> fog max height
        // cb_effects2.x -> fog global thickness
        // cb_effects3.x -> fog ground thickness

        float fogNear           = 0.5;
        float fogFar            = 1000.0;
        float depth             = length(wolrdPos - cb_camPos.xyz);
        float groundHeightMin   = -2.0;
        float groundHeightMax   = cb_effects2.w;
        float globalThickness   = cb_effects2.x;
        float groundThickness   = cb_effects3.x;
        float depthFactor       = clamp(1.0 - (fogFar - depth) / (fogFar - fogNear), 0.0, 1.0);
        float heightFactor      = clamp(1.0 - (wolrdPos.y - groundHeightMin) / (groundHeightMax - groundHeightMin), 0.0, 1.0);
        float globalFactor      = depthFactor * globalThickness;
        float noiseFactor       = 1.0;
        float groundFactor      = heightFactor * depthFactor * noiseFactor * groundThickness;
        float fogFactor         = clamp(groundFactor + globalFactor, 0.0, 1.0);
        output.color.xyz        = lerp(output.color.xyz, 1.0, fogFactor);

        // Volumetric light
        if (cb_effects1.y > 0.5)
            output.color.xyz += VolumetricLighting(cb_sun, wolrdPos, input.uv, cb_cascades[0], fogFactor, pc.frameBufferSize);
    }

    return output;
}
