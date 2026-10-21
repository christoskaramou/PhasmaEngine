// Material pass 2: MaterialX's generated shading for visibility pixels of MaterialX materials, under PE's lights,
// shadows and environment (PePrelude.slang over LightingPS's bindings), written over LightOpaque's result.
// Quad-exact: each output pixel's graph inputs are evaluated by a 2x2 quad on its own triangle (see VisibilityDebug.slang).
#define PE_SHADE_ONCE 1 // the BSDF runs once per pixel, after the quads (MaterialShadeMain.slang)
#include "../Gbuffer/Lighting.hlsl"
#include "VisibilityCommon.slang"

[[vk::binding(14, 0)]] Texture2D<uint4> pe_visibility;
[[vk::binding(15, 0)]] ByteAddressBuffer pe_geometry;
[[vk::binding(16, 0)]] ByteAddressBuffer pe_frameData;
[[vk::binding(17, 0)]] StructuredBuffer<Mesh_Constants> pe_constants;
[[vk::binding(18, 0)]] ByteAddressBuffer pe_materialBytes;
[[vk::binding(20, 0)]] ByteAddressBuffer pe_deformed;
[[vk::binding(21, 0)]] ByteAddressBuffer pe_bins;

#ifdef PE_VIEWPORT_RGBA8
[[vk::image_format("rgba8")]][[vk::binding(19, 0)]] RWTexture2D<float4> pe_viewportOut;
#else
[[vk::image_format("rgba16f")]][[vk::binding(19, 0)]] RWTexture2D<float4> pe_viewportOut;
#endif

// Set 2 holds only the scene texture table, so it binds the RHI's shared per-frame table (t0 in space 100 on DX12).
// Textured graphs sample it and the graph samplers; the default module never does, so they are compiled out.
[[vk::binding(0, 2)]] Texture2D pe_textures[] : register(t0, space100);

// The rest goes above ProbeVolume's set-3 bindings 0-5 (Vulkan guarantees only four sets). DX12 maps binding N to register
// N and an array to N..N+size-1, and a space's sampler table spans s0 up to its highest register, so the single samplers
// come before the 18 graph samplers (s8-s25). Fog's blurred sky is LightingPS's cube, which stays in its set 2.
CubeSamplerDecl(6, 3, pe_fogCube)
[[vk::binding(7, 3)]] SamplerState pe_envSampler;
[[vk::binding(8, 3)]] SamplerState pe_samplers[18];

// OpenPBR's environment: lat-long radiance pyramid and irradiance (Skybox::BuildOpenPBREnvironment).
[[vk::binding(9, 3)]] Texture2D<float4> pe_envRadiance;
[[vk::binding(10, 3)]] Texture2D<float4> pe_envIrradiance;

#include "PePrelude.slang"
