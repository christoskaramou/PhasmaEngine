struct Uniforms
{
    column_major float4x4 viewDirectionProjectionInverse;
};

[[vk::binding(0, 0)]] cbuffer UBO : register(b0, space0) { Uniforms uniforms; }
[[vk::binding(1, 0)]] SamplerState     mySampler : register(s1, space0);
[[vk::binding(2, 0)]] TextureCube<float4> myTexture : register(t2, space0);

struct PSInput
{
    float4 svPos                           : SV_Position;
    [[vk::location(0)]] float4 direction   : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target0
{
    float4 t = mul(uniforms.viewDirectionProjectionInverse, input.direction);
    float3 uvw = normalize(t.xyz / t.w);
    return myTexture.Sample(mySampler, uvw);
}
