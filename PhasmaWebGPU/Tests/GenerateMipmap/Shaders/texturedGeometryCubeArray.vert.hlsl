struct Uniforms
{
    column_major float4x4 matrix;
};

[[vk::binding(0, 0)]] ConstantBuffer<Uniforms> g_uni : register(b0);

struct VSInput
{
    float4 position : POSITION;
    float2 uv : TEXCOORD0;
    uint instanceIdx : SV_InstanceID;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 texcoord : TEXCOORD0;
    nointerpolation uint instanceIdx : INSTANCE_IDX;
};

VSOutput VSMain(VSInput v)
{
    VSOutput o;
    o.position = mul(g_uni.matrix, v.position);
    o.texcoord = v.position.xyz;
    o.instanceIdx = v.instanceIdx;
    return o;
}
