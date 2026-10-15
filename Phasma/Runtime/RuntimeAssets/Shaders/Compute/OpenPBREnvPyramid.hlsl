#include "../Common/Common.hlsl"

struct PushConstants
{
    uint mipLevel;
    uint width;
    uint height;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

[[vk::image_format("rgba32f")]][[vk::binding(0)]] RWTexture2D<float4> outMips[13] : register(u0);
#ifdef PE_ENV_FROM_CUBE
[[vk::binding(1)]] TextureCube<float4> inSource : register(t1);
[[vk::binding(2)]] SamplerState samplerSource : register(s2);
#else
[[vk::binding(1)]] Texture2D<float4> inSource : register(t1);
#endif

// The inverse of EquirectangularToCubemap.hlsl's SampleSphericalMap: u = atan2(z, x) / 2pi + 0.5, v = 0.5 - asin(y) / pi.
float3 LatLongDirection(float2 uv)
{
    const float longitude = (uv.x - 0.5) * 2.0 * PI;
    const float latitude = (0.5 - uv.y) * PI;

    return float3(cos(latitude) * cos(longitude), sin(latitude), cos(latitude) * sin(longitude));
}

// Mip-0 radiance of one texel: the source equirect itself, or (skies that were never an equirect: six-face and solid
// colour cubes) the legacy cube resampled at the texel centre.
float3 SourceRadiance(uint2 texel)
{
#ifdef PE_ENV_FROM_CUBE
    const float2 uv = (float2(texel) + 0.5) / float2(pc.width, pc.height);

    return inSource.SampleLevel(samplerSource, LatLongDirection(uv), 0).rgb;
#else
    return inSource.Load(int3(texel, 0)).rgb;
#endif
}

// One lat-long mip per dispatch. Each texel is the plain average of the 2^m x 2^m mip-0 texels it covers, which is what
// glGenerateMipmap gives MaterialX's own lat-long environment.
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint mipWidth = max(1u, pc.width >> pc.mipLevel);
    const uint mipHeight = max(1u, pc.height >> pc.mipLevel);
    if (id.x >= mipWidth || id.y >= mipHeight)
        return;

    const uint footprint = 1u << pc.mipLevel;
    const uint2 origin = id.xy * footprint;
    const uint2 lastTexel = uint2(pc.width - 1u, pc.height - 1u);
    float3 radiance = 0.0;

    for (uint y = 0; y < footprint; ++y)
    {
        for (uint x = 0; x < footprint; ++x)
            radiance += SourceRadiance(min(origin + uint2(x, y), lastTexel));
    }

    outMips[pc.mipLevel][id.xy] = float4(radiance / float(footprint * footprint), 1.0);
}
