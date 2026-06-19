struct PSInput
{
    float4 svPos                          : SV_Position;
    [[vk::location(0)]] float3 fragNormal : TEXCOORD0;
    [[vk::location(1)]] float2 fragUV     : TEXCOORD1;
};

struct PSOutput
{
    float4 normal : SV_Target0;
    float4 albedo : SV_Target1;
};

PSOutput PSMain(PSInput input)
{
    float2 uv = floor(30.0f * input.fragUV);
    float c   = 0.2f + 0.5f * ((uv.x + uv.y) - 2.0f * floor((uv.x + uv.y) / 2.0f));

    PSOutput o;
    o.normal = float4(normalize(input.fragNormal), 1.0f);
    o.albedo = float4(c, c, c, 1.0f);
    return o;
}
