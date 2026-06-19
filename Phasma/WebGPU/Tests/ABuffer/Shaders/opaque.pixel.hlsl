struct PSIn
{
    float4          position : SV_Position;
    nointerpolation uint     instance : INSTANCE;
};

static const float3 kColors[6] =
{
    float3(1.0, 0.0, 0.0),
    float3(0.0, 1.0, 0.0),
    float3(0.0, 0.0, 1.0),
    float3(1.0, 0.0, 1.0),
    float3(1.0, 1.0, 0.0),
    float3(0.0, 1.0, 1.0),
};

float4 PSMain(PSIn input) : SV_Target0
{
    return float4(kColors[input.instance % 6u], 1.0);
}
