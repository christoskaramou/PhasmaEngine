// Composite pass: walks each pixel's linked list, insertion-sorts by depth,
// and blends translucent fragments back-to-front with premultiplied alpha.

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
[[vk::binding(1, 0)]] RWStructuredBuffer<uint>      heads      : register(u1, space0);
[[vk::binding(2, 0)]] RWStructuredBuffer<ListEntry> linkedList : register(u2, space0);
[[vk::binding(3, 0)]] cbuffer SliceInfoCB : register(b3, space0) { SliceInfo sliceInfo; };

static const uint kMaxLayers = 12u;
static const uint kEndOfList = 0xFFFFFFFFu;

struct ResolvedEntry
{
    float4 color;
    float  depth;
};

static float4 UnpackColor(uint c)
{
    float r = (float)((c      ) & 0xFFu) / 255.0;
    float g = (float)((c >>  8) & 0xFFu) / 255.0;
    float b = (float)((c >> 16) & 0xFFu) / 255.0;
    float a = (float)((c >> 24) & 0xFFu) / 255.0;
    return float4(r, g, b, a);
}

float4 PSMain(float4 position : SV_Position) : SV_Target0
{
    int2 fragCoords = int2(position.xy);
    uint headsIndex = 1u
                    + (uint)(fragCoords.y - sliceInfo.sliceStartY) * uniforms.targetWidth
                    + (uint)fragCoords.x;

    ResolvedEntry layers[kMaxLayers];

    uint numLayers    = 0u;
    uint elementIndex = heads[headsIndex];

    // Walk the per-pixel list up to kMaxLayers entries.
    [loop]
    while (elementIndex != kEndOfList && numLayers < kMaxLayers)
    {
        ListEntry e = linkedList[elementIndex];
        layers[numLayers].color = UnpackColor(e.color);
        layers[numLayers].depth = e.depth;
        numLayers++;
        elementIndex = e.next;
    }

    if (numLayers == 0u)
        discard;

    // Insertion-sort back-to-front (largest depth first).
    for (uint i = 1u; i < numLayers; i++)
    {
        ResolvedEntry toInsert = layers[i];
        uint j = i;
        [loop]
        while (j > 0u && toInsert.depth > layers[j - 1u].depth)
        {
            layers[j] = layers[j - 1u];
            j--;
        }
        layers[j] = toInsert;
    }

    // Premultiply alpha for the first (farthest) layer and mix forward.
    float4 color = float4(layers[0].color.a * layers[0].color.rgb, layers[0].color.a);

    for (uint k = 1u; k < numLayers; k++)
    {
        float3 mixed = lerp(color.rgb, layers[k].color.rgb, layers[k].color.aaa);
        color = float4(mixed, color.a);
    }

    return color;
}
