// PhasmaEngine: MaterialX textures sample PE's bindless table. Implicit-LOD sampling is kept on purpose:
// the resolve evaluates each pixel in a 2x2 quad on its own triangle, so derivatives are equivalent to a
// pixel shader's (within measured tolerance). RT hit stages (PE_MATERIAL_HIT) have no derivatives and sample mip 0.
// Keeps every member MaterialX's own struct exposes.
struct SamplerTexture2D
{
    uint textureIndex;
    uint samplerIndex;

    int get_width(int mipLevel = 0)
    {
        uint w, h, levels;
        pe_textures[NonUniformResourceIndex(textureIndex)].GetDimensions(uint(mipLevel), w, h, levels);
        return int(w);
    }

    int get_height(int mipLevel = 0)
    {
        uint w, h, levels;
        pe_textures[NonUniformResourceIndex(textureIndex)].GetDimensions(uint(mipLevel), w, h, levels);
        return int(h);
    }

    int get_num_mip_levels()
    {
        uint w, h, levels;
        pe_textures[NonUniformResourceIndex(textureIndex)].GetDimensions(0u, w, h, levels);
        return int(levels);
    }
};

float4 textureLod(SamplerTexture2D tex, float2 uv, float lod)
{
    return pe_textures[NonUniformResourceIndex(tex.textureIndex)].SampleLevel(pe_samplers[NonUniformResourceIndex(tex.samplerIndex)], uv, lod);
}

float4 texture(SamplerTexture2D tex, float2 uv)
{
#ifdef PE_MATERIAL_HIT
    // RT hit stages have no derivatives: mip 0, as the legacy hit shader samples.
    return pe_textures[NonUniformResourceIndex(tex.textureIndex)].SampleLevel(pe_samplers[NonUniformResourceIndex(tex.samplerIndex)], uv, 0.0);
#else
    return pe_textures[NonUniformResourceIndex(tex.textureIndex)].Sample(pe_samplers[NonUniformResourceIndex(tex.samplerIndex)], uv);
#endif
}

float4 textureGrad(SamplerTexture2D tex, float2 uv, float2 ddx_, float2 ddy_)
{
    return pe_textures[NonUniformResourceIndex(tex.textureIndex)].SampleGrad(pe_samplers[NonUniformResourceIndex(tex.samplerIndex)], uv, ddx_, ddy_);
}

int2 textureSize(SamplerTexture2D tex, int mipLevel)
{
    uint w, h, levels;
    pe_textures[NonUniformResourceIndex(tex.textureIndex)].GetDimensions(uint(mipLevel), w, h, levels);
    return int2(w, h);
}
