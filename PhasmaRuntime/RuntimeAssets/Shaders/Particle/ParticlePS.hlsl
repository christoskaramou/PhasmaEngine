#include "../Common/Structures.hlsl"

[[vk::binding(0, 1)]] Texture2D textures[16];
[[vk::binding(1, 1)]] SamplerState samplerState;

PS_OUTPUT_Particle mainPS(PS_INPUT_Particle input)
{
    PS_OUTPUT_Particle output;

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
    
    // Discard if alpha is too low
    if (output.color.a < 0.01)
        discard;
    
    return output;
}
