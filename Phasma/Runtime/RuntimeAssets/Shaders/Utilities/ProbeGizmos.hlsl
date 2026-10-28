// Editor gizmo (ProbeGizmosPass): every GI probe as a small sphere lit by its own irradiance, the radiance a white
// diffuse surface there would show. World probes come from the probe volume (coarser cascades skip the probes inside
// the next finer one, which shading does not use), screen probes from ScreenProbePass's positions and filtered SH.
// Camera-facing quads after tonemapping, tested against the scene depth as GridPS is.
#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "../Tonemap/Tonemap.hlsl"
#include "../GlobalIllumination/ScreenProbeCommon.hlsl" // ProbeVolume.hlsl's default set: 1, 2, 8, 9

// Scene PerFrameData: viewProjection, previousViewProjection, invView, invProjection.
[[vk::binding(0)]] ByteAddressBuffer data;
TexSamplerDecl(3, 0, Depth)
[[vk::binding(4, 0)]] Texture2D<float4> gizmo_probePositions; // xyz world position, w view distance (0 = no probe)
[[vk::binding(5, 0)]] Texture2D<float4> gizmo_probeSH;        // SCREEN_PROBE_SH_TEXELS texels per probe
[[vk::push_constant]] PushConstants_ProbeGizmos pc;

static const float2 kCorners[6] = {float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
                                   float2(-1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0)};

struct VS_OUTPUT_ProbeGizmo
{
    float4 position : SV_POSITION;
    float2 corner : TEXCOORD0;
    nointerpolation float4 centerRadius : TEXCOORD1;
    nointerpolation uint probe : TEXCOORD2;
};

float4x4 SceneMatrix(uint index)
{
    uint offset = index * 64u;
    float4x4 result;
    result[0] = asfloat(data.Load4(offset));
    result[1] = asfloat(data.Load4(offset + 16));
    result[2] = asfloat(data.Load4(offset + 32));
    result[3] = asfloat(data.Load4(offset + 48));
    return result;
}

bool WorldProbeGizmo(uint probe, out float3 center, out float radius)
{
    const uint cascade = ProbeCascade(probe);
    center = ProbePosition(probe);
    radius = pc.radius * gi_originSpacing[cascade].w;
    if (cascade == 0)
        return true;

    // Inside the finer cascade by a cell, ProbeCascadeAt never picks this one; its edge cells blend both.
    const float3 grid = (center - gi_originSpacing[cascade - 1].xyz) / gi_spacing[cascade - 1].xyz;
    const bool inner = all(grid >= 1.0) && all(grid <= float(gi_gridSize) - 2.0);
    return !inner;
}

bool ScreenProbeGizmo(uint probe, out float3 center, out float radius)
{
    const float4 position = gizmo_probePositions.Load(int3(probe % pc.tilesX, probe / pc.tilesX, 0));
    center = position.xyz;
    radius = pc.radius * position.w;
    return position.w > 0.0;
}

VS_OUTPUT_ProbeGizmo mainVS(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    VS_OUTPUT_ProbeGizmo output;
    output.corner = kCorners[vertex];
    output.probe = instance;

    float3 center;
    float radius;
    const bool visible = pc.mode == 0 ? WorldProbeGizmo(instance, center, radius)
                                      : ScreenProbeGizmo(instance, center, radius);
    output.centerRadius = float4(center, radius);
    if (!visible)
    {
        output.position = 0.0.xxxx; // w = 0 on every corner: nothing rasterizes
        return output;
    }

    const float4x4 invView = SceneMatrix(2);
    const float3 world = center + (output.corner.x * invView[0].xyz + output.corner.y * invView[1].xyz) * radius;
    float4 clip = mul(float4(world, 1.0), SceneMatrix(0));
    clip.xy -= pc.projJitter * clip.w; // drawn after TAA, unjittered as GridPS
    output.position = ApplyViewportYConvention(clip);
    return output;
}

float3 ScreenProbeGizmoIrradiance(uint probe, float3 normal)
{
    const int2 tile = int2(probe % pc.tilesX, probe / pc.tilesX);
    ScreenProbeSH sh;
    [unroll] for (uint i = 0; i < SCREEN_PROBE_SH_TEXELS; ++i)
        sh.c[i] = gizmo_probeSH.Load(int3(tile.x * SCREEN_PROBE_SH_TEXELS + i, tile.y, 0)).rgb;

    return ScreenProbeSHIrradiance(sh, normal);
}

PS_OUTPUT_Color mainPS(VS_OUTPUT_ProbeGizmo input)
{
    const float r2 = dot(input.corner, input.corner);
    if (r2 > 1.0)
        discard;

    const float4x4 invView = SceneMatrix(2);
    const float3 camera = invView[3].xyz;
    const float3 center = input.centerRadius.xyz;
    const float3 toCamera = normalize(camera - center);
    const float3 normal = normalize(input.corner.x * invView[0].xyz + input.corner.y * invView[1].xyz +
                                    sqrt(1.0 - r2) * toCamera);
    const float3 surface = center + normal * input.centerRadius.w;

    // The scene depth is jittered and may be lower resolution; zero is sky.
    const float2 sceneUV = NdcToUv(UvToNdc(input.position.xy * pc.invDisplaySize) + pc.projJitter);
    uint depthWidth, depthHeight;
    Depth.GetDimensions(depthWidth, depthHeight);
    const int2 depthSize = int2(depthWidth, depthHeight);
    const int2 depthPixel = clamp(int2(sceneUV * float2(depthSize)), int2(0, 0), depthSize - 1);
    const float sceneDepth = Depth.Load(int3(depthPixel, 0)).x;
    if (sceneDepth > 0.0)
    {
        const float3 sceneWorld = mul(float4(GetPosFromUV(sceneUV, sceneDepth, SceneMatrix(3)), 1.0), invView).xyz;
        if (distance(surface, camera) > distance(sceneWorld, camera) + 0.002)
            discard;
    }

    float3 irradiance;
    if (pc.mode == 0)
        irradiance = ProbeSample(gi_irradiance, input.probe, normal).rgb * gi_intensity;
    else
        irradiance = ScreenProbeGizmoIrradiance(input.probe, normal);

    PS_OUTPUT_Color output;
    output.color = float4(LinearToSrgb(ACESFitted(irradiance / PI)), 1.0);
    return output;
}
