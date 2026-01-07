#include "../Common/Structures.hlsl"

[[vk::binding(0, 1)]] Texture2D textures[16];
[[vk::binding(1, 1)]] SamplerState samplerState;

PS_OUTPUT_Particle mainPS(PS_INPUT_Particle input)
{
    PS_OUTPUT_Particle output;

    // Sample Texture
    uint texID = (uint)input.textureIndex;
    float4 texColor = textures[texID].Sample(samplerState, input.uv);
    
    // Combine with particle color
    output.color = input.color * texColor;
    
    // Discard if alpha is too low
    if (output.color.a < 0.01)
        discard;
    
    return output;
}
