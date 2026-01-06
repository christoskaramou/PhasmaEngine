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

    float2 centered = input.uv - 0.5;
    float dist = length(centered) * 2.0;
    float soft = saturate(1.0 - dist);
    soft = soft * soft;
    float vertical = smoothstep(0.0, 1.0, input.uv.y);
    
    // Soft edges + stronger base
    output.color.a *= soft * vertical;
    
    // Hot core boost
    float core = saturate(1.0 - dist * 1.3);
    output.color.rgb = lerp(output.color.rgb, output.color.rgb * 1.2 + float3(0.2, 0.1, 0.0), core * 0.5);
    
    // Discard if alpha is too low
    if (output.color.a < 0.005)
        discard;
    
    return output;
}
