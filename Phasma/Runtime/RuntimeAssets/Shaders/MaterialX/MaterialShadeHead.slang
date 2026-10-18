// Material pass 2: MaterialX's generated shading for visibility pixels of MaterialX materials, under PE's lights,
// shadows and environment (PePrelude.slang over LightingPS's bindings), written over LightOpaque's result.
// Quad-exact: each output pixel is evaluated by a 2x2 quad on its own triangle (see VisibilityDebug.slang).
#include "../Gbuffer/Lighting.hlsl"
#include "VisibilityCommon.slang"

[[vk::binding(14, 0)]] Texture2D<uint4> pe_visibility;
[[vk::binding(15, 0)]] ByteAddressBuffer pe_geometry;
[[vk::binding(16, 0)]] ByteAddressBuffer pe_frameData;
[[vk::binding(17, 0)]] StructuredBuffer<Mesh_Constants> pe_constants;
[[vk::binding(18, 0)]] ByteAddressBuffer pe_materialBytes;

#ifdef PE_VIEWPORT_RGBA8
[[vk::image_format("rgba8")]][[vk::binding(19, 0)]] RWTexture2D<float4> pe_viewportOut;
#else
[[vk::image_format("rgba16f")]][[vk::binding(19, 0)]] RWTexture2D<float4> pe_viewportOut;
#endif

// Set 2 (sets 0, 1 and 3 are LightingPS's and ProbeVolume's). DX12 maps binding N to register N and an array to N..N+size-1,
// so the 18 graph samplers (s2-s19) sit above the cube's s0 and the environment's s1; the unbounded texture array is the
// set's last binding (t0 in space 100 on DX12). No set is free for it alone, so the pass keeps its own copy of the scene
// texture table. Textured graphs sample these; the default module never does, so they are compiled out.
[[vk::binding(6, 2)]] Texture2D pe_textures[] : register(t0, space100);
[[vk::binding(2, 2)]] SamplerState pe_samplers[18];

// OpenPBR's environment: lat-long radiance pyramid and irradiance (Skybox::BuildOpenPBREnvironment).
[[vk::binding(3, 2)]] Texture2D<float4> pe_envRadiance;
[[vk::binding(4, 2)]] Texture2D<float4> pe_envIrradiance;
[[vk::binding(1, 2)]] SamplerState pe_envSampler;

#include "PePrelude.slang"
