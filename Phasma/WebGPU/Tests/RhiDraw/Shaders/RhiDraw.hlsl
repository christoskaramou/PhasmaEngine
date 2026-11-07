// RhiDraw (WebGPU RHI spec P2 exit): one entry point per check, baked like every engine shader. Self-contained, so
// the cache keys depend on this file only. Positions are Vulkan clip space (y down); the bake flips them for WebGPU.
struct Push
{
    float4 color;
    float lod;
    float depth;
    float2 pad;
};

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

[[vk::binding(0, 0)]] RWStructuredBuffer<float4> OutColors;
[[vk::binding(1, 0)]] StructuredBuffer<float4> InColors;
[[vk::combinedImageSampler]] [[vk::binding(2, 0)]] Texture2D Tex;
[[vk::combinedImageSampler]] [[vk::binding(2, 0)]] SamplerState sampler_Tex;
[[vk::binding(3, 0)]] Texture2D<float> DepthTex;
[[vk::push_constant]] Push pc;

// Two clockwise triangles (front faces under the engine's winding) covering the viewport.
static const float2 kQuad[6] = {float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, -1), float2(1, 1), float2(-1, 1)};

// A clockwise triangle on the left half (front) and a counter-clockwise one on the right half (culled).
static const float2 kCull[6] = {float2(-1, -1), float2(-0.1, -1), float2(-1, 1), float2(0.1, -1), float2(1, 1), float2(1, -1)};

VSOut Corner(float2 position, float z, float4 color)
{
    VSOut output;
    output.position = float4(position, z, 1.0);
    output.uv = position * 0.5 + 0.5;
    output.color = color;
    return output;
}

// The push color, scaled, so the buffer the next draw reads proves both the dispatch and the push slot.
[numthreads(4, 1, 1)]
void mainCS(uint3 id : SV_DispatchThreadID)
{
    OutColors[id.x] = float4(pc.color.rgb * 0.5, 1.0);
}

VSOut DepthVS(uint vertexId : SV_VertexID)
{
    return Corner(kQuad[vertexId], pc.depth, 0.0);
}

VSOut CullVS(uint vertexId : SV_VertexID)
{
    return Corner(kCull[vertexId], 0.0, pc.color);
}

VSOut QuadVS(uint vertexId : SV_VertexID)
{
    return Corner(kQuad[vertexId], 0.0, pc.color);
}

// PE_DRAW_ID is the engine's per-instance vertex input; an indirect draw's firstInstance picks the element.
VSOut InstVS(uint vertexId : SV_VertexID, uint drawId : PE_DRAW_ID)
{
    return Corner(kQuad[vertexId], 0.0, float4(drawId * 40.0 / 255.0, 0.0, 0.0, 1.0));
}

float4 ColorPS(VSOut input) : SV_Target0
{
    return input.color;
}

float4 TexPS(VSOut input) : SV_Target0
{
    return Tex.SampleLevel(sampler_Tex, input.uv, pc.lod);
}

float4 DepthLoadPS(VSOut input) : SV_Target0
{
    return float4(DepthTex.Load(int3(input.position.xy, 0)), 0.0, 0.0, 1.0);
}

float4 BufPS(VSOut input) : SV_Target0
{
    return InColors[0];
}
