// Textured point: samples the butterfly atlas and discards near-transparent
// fragments, matching upstream textured.frag.wgsl.

[[vk::binding(1, 0)]] SamplerState samp : register(s1, space0);
[[vk::binding(2, 0)]] Texture2D<float4> tex : register(t2, space0);

struct PSInput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float2 texcoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target0
{
    float4 color = tex.Sample(samp, input.texcoord);
    if (color.a < 0.1f)
    {
        discard;
    }
    return color;
}
