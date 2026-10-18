// openpbr_debug: quad-exact reconstruction of the raster G-buffer from the visibility payload. Each output pixel
// is evaluated by a 2x2 quad whose four lanes all use the pixel's own triangle at their own pixel centres
// (extrapolated barycentrics, like hardware helper lanes), so ddx/ddy and implicit-LOD Sample() are equivalent
// to a pixel shader rasterising that triangle. Output codes are decoded by .testpool/tests/openpbr_vis.py.
#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "../Common/MaterialFlags.hlsl"
#include "VisibilityCommon.slang"

struct PushConstants_VisibilityDebug
{
    uint verticesOffset;
    uint linearColor;
};

[[vk::push_constant]] PushConstants_VisibilityDebug pc;

[[vk::binding(0, 0)]] Texture2D<uint4> pe_visibility;
[[vk::binding(1, 0)]] Texture2D<float> pe_depth;
[[vk::binding(2, 0)]] Texture2D<float4> pe_albedo;
[[vk::binding(3, 0)]] ByteAddressBuffer pe_geometry;
[[vk::binding(4, 0)]] ByteAddressBuffer pe_frameData;
[[vk::binding(5, 0)]] StructuredBuffer<Mesh_Constants> pe_constants;
[[vk::binding(6, 0)]] StructuredBuffer<MaterialGpuData> pe_materialTable;
[[vk::binding(7, 0)]] ByteAddressBuffer pe_materialBytes;
[[vk::image_format("rgba8")]][[vk::binding(8, 0)]] RWTexture2D<float4> pe_debug;

[[vk::binding(0, 1)]] SamplerState pe_materialSampler;
[[vk::binding(0, 2)]] Texture2D pe_textures[] : register(t0, space100);

// GBufferPS.hlsl's material record (byte path first, then the structured table).
MaterialGpuData PeMaterial(uint id)
{
    const uint byteOff = pe_constants[id].materialByteOffset;
    if (byteOff == 0xFFFFFFFF)
        return pe_materialTable[pe_constants[id].materialId];

    MaterialGpuData m;
    m.baseColorFactor = asfloat(pe_materialBytes.Load4(byteOff));
    m.emissiveTransmission = asfloat(pe_materialBytes.Load4(byteOff + 16));
    m.pbrParams = asfloat(pe_materialBytes.Load4(byteOff + 32));
    m.transmissionVolume = asfloat(pe_materialBytes.Load4(byteOff + 48));
    m.attenuationColor = asfloat(pe_materialBytes.Load4(byteOff + 64));
    return m;
}

// GBufferPS.hlsl's albedo output: base texture x vertex colour x factor, sRGB-encoded under linear_color.
float3 PeGBufferAlbedo(uint id, float2 uv, float4 vertexColor)
{
    const MaterialGpuData mat = PeMaterial(id);
    const uint index = uint(pe_constants[id].meshImageIndex[0]);

    float4 sampled = float4(1.0, 1.0, 1.0, 1.0);
    if (HasTexture(pe_constants[id].textureMask, TEX_BASE_COLOR_BIT) && index != 0xFFFFFFFF)
        sampled = pe_textures[NonUniformResourceIndex(index)].Sample(pe_materialSampler, uv);

    float4 authored = vertexColor * mat.baseColorFactor;
    if (pc.linearColor != 0)
        authored.rgb = SrgbToLinear(vertexColor.rgb) * SrgbToLinear(mat.baseColorFactor.rgb);

    const float4 combined = sampled * authored;
    return pc.linearColor != 0 ? LinearToSrgb(combined.rgb) : combined.rgb;
}

[numthreads(8, 8, 1)]
[DerivativeGroupQuad]
void mainCS(uint3 tid : SV_DispatchThreadID)
{
    const uint2 pixel = tid.xy >> 1; // 4 threads per output pixel (debug view: simple, 4x cost)
    const uint2 lane = tid.xy & 1u;
    const uint2 lanePixel = (pixel & ~1u) + lane; // where this lane evaluates: its own position in pixel's 2x2 quad

    uint2 size;
    pe_visibility.GetDimensions(size.x, size.y);
    const bool inside = all(pixel < size);
    const uint4 vis = inside ? pe_visibility[pixel] : uint4(0, 0, 0, 0);
    const bool owner = all(lane == (pixel & 1u)); // the lane that sits on the output pixel

    // Quad-uniform control flow: the four lanes of a quad share pixel, so they share vis and exit together.
    if (vis.x == 0u)
    {
        if (inside && owner)
            pe_debug[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const PeTriangle tri = PeLoadTriangle(pe_geometry, pe_frameData, pe_constants, pc.verticesOffset, vis);
    const PeSurfacePoint sp = PeSurfaceAt(tri, float2(lanePixel) + 0.5, float2(size));
    const float3 albedo = PeGBufferAlbedo(vis.x - 1u, sp.uv0, sp.color0);
    if (!owner)
        return;

    const float3 mine = round(saturate(albedo) * 255.0);
    const float3 raster = round(pe_albedo[pixel].rgb * 255.0);
    const float depthErr = abs(sp.svPosition.z - pe_depth[pixel]);
    const float albedoErr = max(max(abs(mine.r - raster.r), abs(mine.g - raster.g)), abs(mine.b - raster.b));
    const float depthCode = depthErr == 0.0 ? 255.0 : clamp(floor(-log10(depthErr) * 20.0), 0.0, 254.0);
    const bool finite = !isnan(sp.svPosition.z) && !isinf(sp.svPosition.z) && !any(isnan(albedo)) && !any(isinf(albedo));

    pe_debug[pixel] = float4(depthCode, min(255.0, albedoErr), 255.0, finite ? 255.0 : 0.0) / 255.0;
}
