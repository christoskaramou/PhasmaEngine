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

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 texcoord : TEXCOORD0;
};

struct VSOutput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float3 worldNormal : TEXCOORD0;
    [[vk::location(1)]] float2 fragUV : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    float4 worldPos = mul(uniforms.world, float4(input.position, 1.0f));
    o.svPos = mul(shared_.viewProjection, worldPos);
    o.worldNormal = mul(uniforms.world, float4(input.normal, 0.0f)).xyz;
    o.fragUV = input.texcoord;
    return o;
}
