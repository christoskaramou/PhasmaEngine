// Shared definitions for webgpu-bundle-culling port.
// Ported 1:1 from tiny-webgpu-demo.js (CameraUniforms) and the inline WGSL in
// index.html (Material + Instance + CulledInstances + IndirectArgs structs).

#ifndef BUNDLE_CULLING_COMMON_INC_HLSL
#define BUNDLE_CULLING_COMMON_INC_HLSL

struct CameraUniforms
{
    column_major float4x4 projection;
    column_major float4x4 view;
    float3 position;
    float timeSec;
    float2 zRange;
    float4 frustum[6];
};

#endif // BUNDLE_CULLING_COMMON_INC_HLSL
