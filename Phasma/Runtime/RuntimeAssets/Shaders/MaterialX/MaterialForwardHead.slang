// OpenPBR forward pass (Phase 6): MaterialX's generated shading for alpha-blend and transmission meshes in a pixel shader
// with native derivatives, under material pass 2's lights, shadows and environment (PePrelude.slang over LightingPS's
// bindings), blended over the lit scene by OpenPBRForwardPass. The vertex shader is GBufferVS.hlsl with
// PE_OPENPBR_FORWARD, which reads its frame data and Mesh_Constants at this layout's set-0 bindings 16 and 17.
#define PE_FORWARD 1
#include "../Gbuffer/Lighting.hlsl"
#include "VisibilityCommon.slang"

[[vk::binding(17, 0)]] StructuredBuffer<Mesh_Constants> pe_constants;
[[vk::binding(18, 0)]] ByteAddressBuffer pe_materialBytes;

// The scene texture table and the OpenPBR environment at material pass 2's numbers (MaterialShadeHead.slang).
[[vk::binding(0, 2)]] Texture2D pe_textures[] : register(t0, space100);
CubeSamplerDecl(6, 3, pe_fogCube)
[[vk::binding(7, 3)]] SamplerState pe_envSampler;
[[vk::binding(8, 3)]] SamplerState pe_samplers[18];
[[vk::binding(9, 3)]] Texture2D<float4> pe_envRadiance;
[[vk::binding(10, 3)]] Texture2D<float4> pe_envIrradiance;

#include "PePrelude.slang"
