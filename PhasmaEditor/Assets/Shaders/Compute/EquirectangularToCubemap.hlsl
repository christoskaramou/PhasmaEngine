
#include "../Common/Common.hlsl"

RWTexture2DArray<float4> outCubeMap : register(u0);
Texture2D<float4> inEquirectangular : register(t1);
SamplerState samplerLinear : register(s2);

float2 SampleSphericalMap(float3 v)
{
    float2 uv = float2(atan2(v.z, v.x), asin(v.y));
    uv *= float2(0.1591, 0.3183); // inv(2*PI), inv(PI)
    uv += 0.5;
    return float2(uv.x, 1.0 - uv.y);
}

[numthreads(32, 32, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint width, height, elements;
    outCubeMap.GetDimensions(width, height, elements);

    if (id.x >= width || id.y >= height)
        return;

    float2 uv = (float2(id.xy) + 0.5) / float2(width, height);
    uv = uv * 2.0 - 1.0;

    float3 dir;
    switch (id.z)
    {
    case 0: // +X
        dir = normalize(float3(1.0, -uv.y, -uv.x));
        break;
    case 1: // -X
        dir = normalize(float3(-1.0, -uv.y, uv.x));
        break;
    case 2: // +Y
        dir = normalize(float3(uv.x, 1.0, uv.y));
        break;
    case 3: // -Y
        dir = normalize(float3(uv.x, -1.0, -uv.y));
        break;
    case 4: // +Z
        dir = normalize(float3(uv.x, -uv.y, 1.0));
        break;
    case 5: // -Z
        dir = normalize(float3(-uv.x, -uv.y, -1.0));
        break;
    }

    float2 srcUV = SampleSphericalMap(dir);
    float4 color = inEquirectangular.SampleLevel(samplerLinear, srcUV, 0);

    outCubeMap[id] = color;
}
