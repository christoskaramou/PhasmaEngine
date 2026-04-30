// Culled geometry vertex shader. Each instance index is looked up through the
// culled.instances[] array produced by the cull compute pass. Matches upstream
// GEOMETRY_SHADER(geometry, culled = true).

#include "common.inc.hlsl"

[[vk::binding(0, 0)]] cbuffer CameraCB : register(b0, space0)
{
    CameraUniforms camera;
};

struct Material
{
    float4 color;
};
[[vk::binding(0, 1)]] cbuffer MaterialCB : register(b0, space1)
{
    Material material;
};

[[vk::binding(0, 2)]] StructuredBuffer<column_major float4x4> instances : register(t0, space2);

// CulledInstances layout (byte-addressable):
//   +0  : uint indirectIndex
//   +4  : uint instances[MAX_INSTANCES_PER_DRAWABLE]
[[vk::binding(1, 2)]] ByteAddressBuffer culled : register(t1, space2);

struct VSIn
{
    float3 pos : POSITION;
    float3 norm : NORMAL;
    float2 uv0 : TEXCOORD0;
    uint instanceIndex : SV_InstanceID;
};

struct VSOut
{
    float4 svPos : SV_Position;
    float3 norm : NORMAL;
    float2 uv0 : TEXCOORD0;
};

VSOut VSMain(VSIn input)
{
    VSOut output;
    // The culled list packs indirectIndex at offset 0, then uint instances[].
    // input.instanceIndex selects an entry in that array.
    uint idx = culled.Load(4u + input.instanceIndex * 4u);
    float4x4 model = instances[idx];
    float4 worldPos = mul(model, float4(input.pos, 1.0));
    float4 viewPos = mul(camera.view, worldPos);
    output.svPos = mul(camera.projection, viewPos);
    output.svPos.y = -output.svPos.y;

    float3 viewNorm = mul(camera.view, mul(model, float4(input.norm, 0.0))).xyz;
    output.norm = normalize(viewNorm);
    output.uv0 = input.uv0;
    return output;
}
