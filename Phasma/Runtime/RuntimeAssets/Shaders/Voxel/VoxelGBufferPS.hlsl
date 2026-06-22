// VoxelGBufferPS.hlsl — custom GBuffer pixel shader for the voxel subsystem.
//
// Spike 0B / Task 8 artifact. Declares the block atlas as a Texture2DArray and
// samples the layer chosen by the vertex shader's flat tileLayer (sourced from
// joints[0]). The uv is frac()-tiled so a greedy-merged quad whose uv spans
// 0..w / 0..h repeats the tile once per block. Writes the same six GBuffer MRT
// targets as the stock GBuffer PS so voxel chunks composite identically.
//
// Texture2DArray binding: the engine reflector is dimension-agnostic
// (ImageReflection records no view type — Core/Code/API/Reflection.h), and both
// backends emit a true array SRV for PE_IMAGE_VIEW_TYPE_2D_ARRAY
// (Dx12ImageViewImpl::SrvDimension -> D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
// VulkanImageImpl). gVoxelAtlas is bound from Material::namedTextures["gVoxelAtlas"]
// via Scene::UpdateImageViews(); see Task 8 for the descriptor wiring.

#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

[[vk::push_constant]] PushConstants_GBuffer pc;
[[vk::binding(0, 1)]] SamplerState voxel_sampler : register(s0, space1);
[[vk::binding(1, 1)]] Texture2DArray<float4> gVoxelAtlas : register(t1, space1);

// Mirror of VoxelGBufferVS's VS_OUTPUT_Voxel (minus SV_POSITION) for the PS input.
struct PS_INPUT_Voxel
{
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
    float4 tangent : TEXCOORD4;
    float4 color : COLOR;
    float4 positionCS : POSITION0;
    float4 prevPositionCS : POSITION1;
    float4 positionWS : POSITION2;
    nointerpolation uint id : TEXCOORD5;
    nointerpolation uint tileLayer : TEXCOORD6;
};

PS_OUTPUT_Gbuffer mainPS(PS_INPUT_Voxel input)
{
    PS_OUTPUT_Gbuffer output;

    // frac() tiles the atlas layer once per block across a merged quad.
    float3 sampleCoord = float3(frac(input.uv), (float)input.tileLayer);
    float4 albedo = gVoxelAtlas.Sample(voxel_sampler, sampleCoord);

    float4 combinedColor = albedo * input.color; // vertex color reserved for Phase 3 light/AO
    if (combinedColor.a <= 0.0f)
        discard;

    float3 N = normalize(input.normal);

    output.normal = float4(N * 0.5f + 0.5f, 1.0f);
    output.albedo = float4(combinedColor.xyz, combinedColor.a);
    // Opaque voxels: full occlusion, max roughness, no metal, no transmission.
    output.metRough = float4(1.0f, 1.0f, 0.0f, 0.0f);
    output.emissive = float4(0.0f, 0.0f, 0.0f, combinedColor.a);
    output.transparency = 0.0f;

    // Velocity (matches stock GBuffer PS).
    float2 currentNDC = input.positionCS.xy / input.positionCS.w;
    float2 previousNDC = input.prevPositionCS.xy / input.prevPositionCS.w;
    currentNDC -= float2(pc.projJitter.x, pc.projJitter.y);
    previousNDC -= float2(pc.prevProjJitter.x, pc.prevProjJitter.y);
    output.velocity = previousNDC - currentNDC;

    return output;
}
