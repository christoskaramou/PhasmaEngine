// OpenPBR environment helpers shared by the raster prelude (PePrelude.slang) and the RT hit prelude
// (MaterialHitHead.slang). Needs pe_envRadiance and pe_envIrradiance declared before inclusion.
#ifndef PE_ENVIRONMENT
#define PE_ENVIRONMENT

// Direction -> lat-long uv in PE's equirect convention (EquirectangularToCubemap.hlsl); OpenPBREnvPyramid.hlsl fills the
// radiance pyramid through the inverse, with the same exact constants. A pole direction has no azimuth and atan2(0, 0)
// differs between SPIR-V and DXIL, so it gets 0.
float2 PeLatLongUv(float3 dir)
{
    const float azimuth = (dir.x == 0.0 && dir.z == 0.0) ? 0.0 : atan2(dir.z, dir.x);

    return float2(azimuth / (2.0 * PI) + 0.5, 0.5 - asin(clamp(dir.y, -1.0, 1.0)) / PI);
}

// The irradiance map's rows run from the zenith to the nadir inclusive (OpenPBREnvIrradiance.hlsl), so v = 0 and v = 1 are
// the exact poles.
float2 PeIrradianceUv(float3 dir)
{
    uint width;
    uint height;
    pe_envIrradiance.GetDimensions(width, height);

    const float2 uv = PeLatLongUv(dir);

    return float2(uv.x, (uv.y * float(height - 1u) + 0.5) / float(height));
}

float pe_env_max_mip()
{
    uint width;
    uint height;
    uint levels;
    pe_envRadiance.GetDimensions(0, width, height, levels);
    return float(levels - 1u);
}

// MaterialX's own mx_latlong_compute_lod (lib/mx_microfacet_specular.glsl), on a lat-long pyramid built like its mipmaps.
float pe_env_lod(float3 dir, float pdf, float maxMip, int samples)
{
    const float mipLevelOffset = 1.5;
    const float distortion = sqrt(1.0 - dir.y * dir.y);

    return max(maxMip - mipLevelOffset - 0.5 * log2(float(samples) * pdf * distortion), 0.0);
}

#endif
