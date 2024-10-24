#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

static const int MAX_DATA_SIZE = 2048;

[[vk::push_constant]] PushConstants_DepthPass pc;

[[vk::binding(0, 1)]] SamplerState material_sampler;
[[vk::binding(1, 1)]] Texture2D textures[];

float4 GetBaseColor(float2 uv)
{
    uint index = pc.primitiveImageIndex[0];
    return textures[index].Sample(material_sampler, uv);
}

void mainPS(PS_INPUT_Position_Uv input)
{
    float alpha = GetBaseColor(input.uv).a;
    if (alpha < pc.alphaCut)
        discard;
}
