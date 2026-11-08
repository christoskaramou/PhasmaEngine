#include "../Common/Structures.hlsl"

[[vk::binding(16, 1)]] Texture2D<float> sceneDepth; // after textures[16] (t0..t15 on DX12)

#ifdef PE_WEB_PROFILE
// The web profile removes texture arrays (spec decision 6): each particle texture gets its own binding, 20 + k, which
// ParticlePass fills when binding 0 is absent.
[[vk::binding(20, 1)]] Texture2D particleTexture0;
[[vk::binding(21, 1)]] Texture2D particleTexture1;
[[vk::binding(22, 1)]] Texture2D particleTexture2;
[[vk::binding(23, 1)]] Texture2D particleTexture3;
[[vk::binding(24, 1)]] Texture2D particleTexture4;
[[vk::binding(25, 1)]] Texture2D particleTexture5;
[[vk::binding(26, 1)]] Texture2D particleTexture6;
[[vk::binding(27, 1)]] Texture2D particleTexture7;
[[vk::binding(28, 1)]] Texture2D particleTexture8;
[[vk::binding(29, 1)]] Texture2D particleTexture9;
[[vk::binding(30, 1)]] Texture2D particleTexture10;
[[vk::binding(31, 1)]] Texture2D particleTexture11;
[[vk::binding(32, 1)]] Texture2D particleTexture12;
[[vk::binding(33, 1)]] Texture2D particleTexture13;
[[vk::binding(34, 1)]] Texture2D particleTexture14;
[[vk::binding(35, 1)]] Texture2D particleTexture15;
#else
[[vk::binding(0, 1)]] Texture2D textures[16];
#endif
[[vk::binding(1, 1)]] SamplerState samplerState;

#ifdef PE_WEB_PROFILE
// WGSL samples with implicit derivatives only in uniform control flow, and the texture differs per particle: take the
// derivatives first and sample each branch with them, which picks the same mip.
float4 SampleParticleTexture(uint id, float2 uv)
{
    const float2 dx = ddx(uv);
    const float2 dy = ddy(uv);
    if (id == 15u)
        return particleTexture15.SampleGrad(samplerState, uv, dx, dy);
    if (id == 14u)
        return particleTexture14.SampleGrad(samplerState, uv, dx, dy);
    if (id == 13u)
        return particleTexture13.SampleGrad(samplerState, uv, dx, dy);
    if (id == 12u)
        return particleTexture12.SampleGrad(samplerState, uv, dx, dy);
    if (id == 11u)
        return particleTexture11.SampleGrad(samplerState, uv, dx, dy);
    if (id == 10u)
        return particleTexture10.SampleGrad(samplerState, uv, dx, dy);
    if (id == 9u)
        return particleTexture9.SampleGrad(samplerState, uv, dx, dy);
    if (id == 8u)
        return particleTexture8.SampleGrad(samplerState, uv, dx, dy);
    if (id == 7u)
        return particleTexture7.SampleGrad(samplerState, uv, dx, dy);
    if (id == 6u)
        return particleTexture6.SampleGrad(samplerState, uv, dx, dy);
    if (id == 5u)
        return particleTexture5.SampleGrad(samplerState, uv, dx, dy);
    if (id == 4u)
        return particleTexture4.SampleGrad(samplerState, uv, dx, dy);
    if (id == 3u)
        return particleTexture3.SampleGrad(samplerState, uv, dx, dy);
    if (id == 2u)
        return particleTexture2.SampleGrad(samplerState, uv, dx, dy);
    if (id == 1u)
        return particleTexture1.SampleGrad(samplerState, uv, dx, dy);
    return particleTexture0.SampleGrad(samplerState, uv, dx, dy);
}
#define PE_PARTICLE_SAMPLE(id, uv) SampleParticleTexture(id, uv)
#else
#define PE_PARTICLE_SAMPLE(id, uv) textures[id].Sample(samplerState, uv)
#endif

struct ParticleDepthInput
{
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    nointerpolation float textureIndex : TEXCOORD1;
    float2 uv2 : TEXCOORD2;
    float blendFactor : TEXCOORD3;
    nointerpolation float4 depthParameters : TEXCOORD4;
};

PS_OUTPUT_Particle mainPS(ParticleDepthInput input)
{
    PS_OUTPUT_Particle output;

    uint depthWidth, depthHeight;
    sceneDepth.GetDimensions(depthWidth, depthHeight);
    float2 outputSize = float2(abs(input.depthParameters.x), input.depthParameters.y);
    int2 depthPixel = clamp(int2(input.pos.xy / outputSize * float2(depthWidth, depthHeight)),
                           int2(0, 0), int2(depthWidth, depthHeight) - 1);
    float surfaceDepth = sceneDepth.Load(int3(depthPixel, 0));
    float gap = input.depthParameters.x < 0.0
                    ? (input.pos.z - surfaceDepth) * input.depthParameters.z
                    : input.depthParameters.z / max(surfaceDepth, 1e-8) - input.depthParameters.z / max(input.pos.z, 1e-8);
    if (gap <= 0.0)
        discard;

    // Sample Texture
    uint texID = (uint)input.textureIndex;
    float4 texColor = PE_PARTICLE_SAMPLE(texID, input.uv);
    
    if (input.blendFactor > 0.0)
    {
        float4 texColor2 = PE_PARTICLE_SAMPLE(texID, input.uv2);
        texColor = lerp(texColor, texColor2, input.blendFactor);
    }
    
    // Combine with particle color
    output.color = input.color * texColor;
    output.color *= saturate(gap / 0.10); // Screen blending needs RGB faded too, over the last 10 cm.
    
    // Discard if alpha is too low
    if (output.color.a < 0.01)
        discard;
    
    return output;
}
