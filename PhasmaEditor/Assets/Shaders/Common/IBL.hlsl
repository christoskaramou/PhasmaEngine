#ifndef IBL_H_
#define IBL_H_

// https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_sheen.pdf
float3 ComputeIBL_Common(
    float3 N, 
    float3 V, 
    float3 albedo, 
    float metallic, 
    float roughness, 
    float3 F0,
    float occlusion,
    TextureCube texCube, SamplerState samplerCube,
    float2 envBRDF)
{
    float NdotV = saturate(dot(N, V));
    float3 R = reflect(-V, N);
    //float2 envBRDF = texLut.SampleLevel(samplerLut, float2(NdotV, roughness), 0).xy;

    float E_spec = envBRDF.x + envBRDF.y;
    float3 energyCompensation = 1.0 + F0 * (1.0 / max(E_spec, 0.001) - 1.0);
    float3 Ess = F0 * envBRDF.x + envBRDF.y; 
    float3 Fr = Ess * energyCompensation;

    float3 kS = Fr;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    float3 irradiance = texCube.SampleLevel(samplerCube, N, 6).rgb; 
    float3 diffuse = irradiance * albedo * occlusion;

    // Specular (Reflection)
    float mip = roughness * roughness * 8.0; 
    float3 prefilteredColor = texCube.SampleLevel(samplerCube, R, mip).rgb;

    float3 specular = prefilteredColor * kS * occlusion;
    
    return kD * diffuse + specular; 
}

#endif // IBL_H_
