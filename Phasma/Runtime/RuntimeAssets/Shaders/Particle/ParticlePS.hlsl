#include "../Common/Structures.hlsl"

[[vk::binding(16, 1)]] Texture2D<float> sceneDepth; // after textures[16] (t0..t15 on DX12)

[[vk::binding(0, 1)]] Texture2D textures[16];
[[vk::binding(1, 1)]] SamplerState samplerState;

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
    float4 texColor = textures[texID].Sample(samplerState, input.uv);
    
    if (input.blendFactor > 0.0)
    {
        float4 texColor2 = textures[texID].Sample(samplerState, input.uv2);
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
