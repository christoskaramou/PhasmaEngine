#include "../Common/Common.hlsl"

struct PushConstants
{
    uint outputWidth;
    uint outputHeight;
    uint sourceWidth;
    uint sourceHeight;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

[[vk::image_format("rgba32f")]][[vk::binding(0)]] RWTexture2D<float4> outIrradiance : register(u0);
[[vk::binding(1)]] Texture2D<float4> inRadiance : register(t1);

// The inverse of EquirectangularToCubemap.hlsl's SampleSphericalMap: u = atan2(z, x) / 2pi + 0.5, v = 0.5 - asin(y) / pi.
float3 LatLongDirection(float2 uv)
{
    const float longitude = (uv.x - 0.5) * 2.0 * PI;
    const float latitude = (0.5 - uv.y) * PI;

    return float3(cos(latitude) * cos(longitude), sin(latitude), cos(latitude) * sin(longitude));
}

// Cosine-weighted radiance over the sphere / pi: radiance-equivalent, the convention of MaterialX's irradiance map
// (its diffuse multiplies the albedo in, with no 1/pi). The source is a coarse mip of the radiance pyramid.
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= pc.outputWidth || id.y >= pc.outputHeight)
        return;

    // Output rows run from the zenith (row 0) to the nadir (last row) inclusive, columns sit at texel centres: a pole
    // direction then reads one exact value whatever its (undefined) azimuth, where a row half a texel off the pole
    // ripples by a few percent with azimuth. PePrelude.slang's PeIrradianceUv is the matching lookup.
    const float normalLongitude = ((float(id.x) + 0.5) / float(pc.outputWidth) - 0.5) * 2.0 * PI;
    const float normalLatitude = (0.5 - float(id.y) / float(pc.outputHeight - 1u)) * PI;
    const float3 normal = float3(cos(normalLatitude) * cos(normalLongitude),
                                 sin(normalLatitude),
                                 cos(normalLatitude) * sin(normalLongitude));
    const float texelAngleU = 2.0 * PI / float(pc.sourceWidth);
    const float texelAngleV = PI / float(pc.sourceHeight);
    float3 irradiance = 0.0;

    for (uint y = 0; y < pc.sourceHeight; ++y)
    {
        const float latitude = (0.5 - (float(y) + 0.5) / float(pc.sourceHeight)) * PI;
        const float solidAngle = cos(latitude) * texelAngleU * texelAngleV;

        for (uint x = 0; x < pc.sourceWidth; ++x)
        {
            const float3 direction = LatLongDirection((float2(x, y) + 0.5) / float2(pc.sourceWidth, pc.sourceHeight));
            const float weight = max(dot(normal, direction), 0.0) * solidAngle;
            irradiance += inRadiance.Load(int3(x, y, 0)).rgb * weight;
        }
    }

    outIrradiance[id.xy] = float4(irradiance / PI, 1.0);
}
