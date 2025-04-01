#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

[[vk::push_constant]] PushConstants_DepthPass pc;
[[vk::binding(0, 1)]] StructuredBuffer<Primitive_Constants> constants;
[[vk::binding(1, 1)]] SamplerState material_sampler;
[[vk::binding(2, 1)]] Texture2D textures[];

float4 SampleArray(float2 uv, float index)
{
    return textures[index].Sample(material_sampler, uv);
}

float4 GetBaseColor(uint id, float2 uv)
{
    return SampleArray(uv, constants[id].primitiveImageIndex[0]);
}

void mainPS(PS_INPUT_Position_Uv_ID input)
{
    const uint id = input.id;
    float alpha = GetBaseColor(id, input.uv).a;
    if (alpha < constants[id].alphaCut)
        discard;
}
