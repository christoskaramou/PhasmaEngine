struct LightData
{
    float4 position;
    float3 color;
    float  radius;
};

[[vk::binding(0, 0)]] RWStructuredBuffer<LightData> lightsBuffer : register(u0, space0);
[[vk::binding(1, 0)]] cbuffer Config : register(b0, space0)
{
    uint  numLights;
    float dtScale;   // frame dt normalized to 60fps (1.0 at 60fps)
    uint2 _configPad;
};
[[vk::binding(2, 0)]] cbuffer LightExtent : register(b1, space0)
{
    float4 extentMin;
    float4 extentMax;
};

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    uint idx = dtid.x;
    if (idx >= numLights)
        return;

    float fi = (float)idx;
    float step = 0.5f + 0.003f * (fi - 64.0f * floor(fi / 64.0f));
    lightsBuffer[idx].position.y = lightsBuffer[idx].position.y - step * dtScale;

    if (lightsBuffer[idx].position.y < extentMin.y)
        lightsBuffer[idx].position.y = extentMax.y;
}
