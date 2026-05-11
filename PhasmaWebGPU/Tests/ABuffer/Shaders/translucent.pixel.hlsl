// Translucent pass: writes no color, atomically appends per-pixel linked-list entries.

struct Uniforms
{
    column_major float4x4 modelViewProjectionMatrix;
    uint     maxStorableFragments;
    uint     targetWidth;
    uint     _pad0;
    uint     _pad1;
};

struct SliceInfo
{
    int  sliceStartY;
    int  _pad0;
    int  _pad1;
    int  _pad2;
};

// Packed 24-byte linked-list entry:
//   color : RGBA8 packed into a uint
//   depth : clip-space z (float)
//   next  : index of previous head (uint, 0xFFFFFFFF = end-of-list sentinel)
struct ListEntry
{
    uint  color;
    float depth;
    uint  next;
    uint  _pad0;
    uint  _pad1;
    uint  _pad2;
};

[[vk::binding(0, 0)]] cbuffer UniformsCB : register(b0, space0) { Uniforms uniforms; };

// heads[0] = global fragment counter, heads[1 + y*width + x] = per-pixel list head.
[[vk::binding(1, 0)]] RWStructuredBuffer<uint>      heads      : register(u1, space0);
[[vk::binding(2, 0)]] RWStructuredBuffer<ListEntry> linkedList : register(u2, space0);
[[vk::binding(3, 0)]] Texture2D<float>              opaqueDepthTexture : register(t3, space0);
[[vk::binding(4, 0)]] cbuffer SliceInfoCB : register(b4, space0) { SliceInfo sliceInfo; };

struct PSIn
{
    float4          position : SV_Position;
    nointerpolation uint     instance : INSTANCE;
};

static const float3 kColors[6] =
{
    float3(1.0, 0.0, 0.0),
    float3(0.0, 1.0, 0.0),
    float3(0.0, 0.0, 1.0),
    float3(1.0, 0.0, 1.0),
    float3(1.0, 1.0, 0.0),
    float3(0.0, 1.0, 1.0),
};

static uint PackColor(float4 rgba)
{
    uint r = (uint)(saturate(rgba.r) * 255.0 + 0.5);
    uint g = (uint)(saturate(rgba.g) * 255.0 + 0.5);
    uint b = (uint)(saturate(rgba.b) * 255.0 + 0.5);
    uint a = (uint)(saturate(rgba.a) * 255.0 + 0.5);
    return r | (g << 8) | (b << 16) | (a << 24);
}

void PSMain(PSIn input)
{
    int2  fragCoords = int2(input.position.xy);
    float opaqueDepth = opaqueDepthTexture.Load(int3(fragCoords, 0)).x;

    // Reject translucent fragments occluded by opaque geometry.
    if (input.position.z >= opaqueDepth)
        discard;

    uint headsIndex = 1u
                    + (uint)(fragCoords.y - sliceInfo.sliceStartY) * uniforms.targetWidth
                    + (uint)fragCoords.x;

    // Allocate a new slot in the linked list.
    uint fragIndex;
    InterlockedAdd(heads[0], 1u, fragIndex);

    // If we're out of space, silently drop the fragment.
    if (fragIndex < uniforms.maxStorableFragments)
    {
        uint lastHead;
        InterlockedExchange(heads[headsIndex], fragIndex, lastHead);

        float4 color = float4(kColors[(input.instance + 3u) % 6u], 0.3);

        linkedList[fragIndex].color = PackColor(color);
        linkedList[fragIndex].depth = input.position.z;
        linkedList[fragIndex].next  = lastHead;
        linkedList[fragIndex]._pad0 = 0;
        linkedList[fragIndex]._pad1 = 0;
        linkedList[fragIndex]._pad2 = 0;
    }
}
