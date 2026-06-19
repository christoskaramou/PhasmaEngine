#include "../Common/Common.hlsl"
#include "../Common/Structures.hlsl"

#ifndef FORWARD_PLUS_TILE_SIZE
#define FORWARD_PLUS_TILE_SIZE 16
#endif

#ifndef FORWARD_PLUS_MAX_LIGHTS_PER_TILE
#define FORWARD_PLUS_MAX_LIGHTS_PER_TILE 128
#endif

[[vk::binding(0, 0)]] RWStructuredBuffer<uint4> TileLightData : register(u0, space0);
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> PointLightIndices : register(u1, space0);
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> SpotLightIndices : register(u2, space0);

[[vk::binding(3, 0)]] cbuffer LightSystemUBO : register(b3, space0)
{
    uint cb_numDirectionalLights;
    uint cb_numPointLights;
    uint cb_numSpotLights;
    uint cb_numAreaLights;
    uint cb_offsetDirectionalLights;
    uint cb_offsetPointLights;
    uint cb_offsetSpotLights;
    uint cb_offsetAreaLights;
};

[[vk::binding(4, 0)]] cbuffer ForwardPlusCullingUBO : register(b4, space0)
{
    float4x4 cb_viewProj;
    float4 cb_cameraRight;
    float4 cb_cameraUp;
    float4 cb_framebufferAndTiles; // xy: framebuffer size, zw: tile counts
};

[[vk::binding(5, 0)]] ByteAddressBuffer LightsBuffer : register(t5, space0);

groupshared uint gs_pointCount;
groupshared uint gs_spotCount;
groupshared uint gs_overflowMask;

PointLight LoadPointLight(uint index)
{
    uint offset = cb_offsetPointLights + index * 32;
    PointLight light;
    light.color = asfloat(LightsBuffer.Load4(offset));
    light.position = asfloat(LightsBuffer.Load4(offset + 16));
    return light;
}

SpotLight LoadSpotLight(uint index)
{
    uint offset = cb_offsetSpotLights + index * 64;
    SpotLight light;
    light.color = asfloat(LightsBuffer.Load4(offset));
    light.position = asfloat(LightsBuffer.Load4(offset + 16));
    light.rotation = asfloat(LightsBuffer.Load4(offset + 32));
    light.params = asfloat(LightsBuffer.Load4(offset + 48));
    return light;
}

bool ProjectToScreen(float3 worldPos, out float2 screenPos)
{
    float4 clip = mul(float4(worldPos, 1.0f), cb_viewProj);
    if (abs(clip.w) < FLT_EPSILON)
        return false;

    float2 uv = NdcToUv(clip.xy / clip.w);
    screenPos = uv * cb_framebufferAndTiles.xy;
    return true;
}

bool SphereOverlapsTile(float3 center, float radius, float2 tileMin, float2 tileMax)
{
    float2 centerScreen;
    if (!ProjectToScreen(center, centerScreen))
        return true;

    float2 rightScreen;
    float2 upScreen;
    bool rightOk = ProjectToScreen(center + normalize(cb_cameraRight.xyz) * radius, rightScreen);
    bool upOk = ProjectToScreen(center + normalize(cb_cameraUp.xyz) * radius, upScreen);

    float screenRadius = max(cb_framebufferAndTiles.x, cb_framebufferAndTiles.y);
    if (rightOk && upOk)
        screenRadius = max(distance(centerScreen, rightScreen), distance(centerScreen, upScreen));

    float2 radius2 = float2(screenRadius, screenRadius);
    float2 lightMin = centerScreen - radius2;
    float2 lightMax = centerScreen + radius2;

    return lightMax.x >= tileMin.x &&
           lightMin.x <= tileMax.x &&
           lightMax.y >= tileMin.y &&
           lightMin.y <= tileMax.y;
}

[numthreads(64, 1, 1)]
void mainCS(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    uint tileCountX = (uint)cb_framebufferAndTiles.z;
    uint tileCountY = (uint)cb_framebufferAndTiles.w;
    if (groupId.x >= tileCountX || groupId.y >= tileCountY)
        return;

    uint tileIndex = groupId.y * tileCountX + groupId.x;
    uint listBase = tileIndex * FORWARD_PLUS_MAX_LIGHTS_PER_TILE;

    if (groupThreadId.x == 0)
    {
        gs_pointCount = 0;
        gs_spotCount = 0;
        gs_overflowMask = 0;
        TileLightData[tileIndex] = uint4(0, 0, 0, 0);
    }

    GroupMemoryBarrierWithGroupSync();

    float2 tileMin = float2(groupId.xy) * FORWARD_PLUS_TILE_SIZE;
    float2 tileMax = min(tileMin + FORWARD_PLUS_TILE_SIZE, cb_framebufferAndTiles.xy);

    for (uint i = groupThreadId.x; i < cb_numPointLights; i += 64)
    {
        PointLight light = LoadPointLight(i);
        if (SphereOverlapsTile(light.position.xyz, light.position.w, tileMin, tileMax))
        {
            uint slot;
            InterlockedAdd(gs_pointCount, 1, slot);
            if (slot < FORWARD_PLUS_MAX_LIGHTS_PER_TILE)
                PointLightIndices[listBase + slot] = i;
            else
                InterlockedOr(gs_overflowMask, 1);
        }
    }

    for (uint j = groupThreadId.x; j < cb_numSpotLights; j += 64)
    {
        SpotLight light = LoadSpotLight(j);
        if (SphereOverlapsTile(light.position.xyz, light.position.w, tileMin, tileMax))
        {
            uint slot;
            InterlockedAdd(gs_spotCount, 1, slot);
            if (slot < FORWARD_PLUS_MAX_LIGHTS_PER_TILE)
                SpotLightIndices[listBase + slot] = j;
            else
                InterlockedOr(gs_overflowMask, 2);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (groupThreadId.x == 0)
    {
        TileLightData[tileIndex] = uint4(min(gs_pointCount, (uint)FORWARD_PLUS_MAX_LIGHTS_PER_TILE),
                                         min(gs_spotCount, (uint)FORWARD_PLUS_MAX_LIGHTS_PER_TILE),
                                         gs_overflowMask,
                                         0);
    }
}
