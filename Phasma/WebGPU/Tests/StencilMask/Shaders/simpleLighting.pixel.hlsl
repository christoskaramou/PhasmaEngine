struct Uniforms
{
    column_major float4x4 world;
    float4 color;
};

struct SharedUniforms
{
    column_major float4x4 viewProjection;
    float3 lightDirection;
    float _pad0;
};

[[vk::binding(0, 0)]] cbuffer UniformsCB : register(b0, space0)
{
    Uniforms uniforms;
}

[[vk::binding(1, 0)]] cbuffer SharedUniformsCB : register(b1, space0)
{
    SharedUniforms shared_;
}

struct PSInput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float3 worldNormal : TEXCOORD0;
    [[vk::location(1)]] float2 fragUV : TEXCOORD1;
};

float4 PSMain(PSInput input) : SV_Target0
{
    float3 n = normalize(input.worldNormal);
    float l = dot(n, shared_.lightDirection) * 0.5f + 0.5f;
    return float4(uniforms.color.rgb * l, uniforms.color.a);
}
