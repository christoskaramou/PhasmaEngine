static const int MAX_DATA_SIZE = 2048; // TODO: calculate on init

struct PushConstants
{
    float2 projJitter;
    uint modelIndex;
    uint meshIndex;
    uint color;
};

float4 UnpackColor(uint color)
{
    float4 col;
    col.r = color >> 24;
    col.g = (color << 8) >> 24;
    col.b = (color << 16) >> 24;
    col.a = (color << 24) >> 24;
    return col / 256.0;
}

[[vk::push_constant]] PushConstants pc;

[[vk::binding(0)]] tbuffer UBO { float4x4 data[MAX_DATA_SIZE]; };

#define meshMatrix data[pc.meshIndex]
#define modelMvp data[pc.modelIndex + 1]

struct VS_INPUT
{
    float3 position : POSITION;
};

struct VS_OUTPUT
{
    float4 position : SV_POSITION;
    float3 color : COLOR;
};

VS_OUTPUT mainVS(VS_INPUT input)
{
    VS_OUTPUT o;
    float3 position = input.position;
    o.position = mul(float4(input.position, 1.0f), mul(meshMatrix, modelMvp));
    o.color = UnpackColor(pc.color).rgb;
    return o;
}
