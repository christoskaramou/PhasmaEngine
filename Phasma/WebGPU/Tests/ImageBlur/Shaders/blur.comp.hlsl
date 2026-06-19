struct Params
{
    int  filterDim;
    uint blockDim;
};

struct Flip
{
    uint value;
};

[[vk::binding(0, 0)]] SamplerState samp : register(s0, space0);
[[vk::binding(1, 0)]] cbuffer ParamsCB : register(b1, space0) { Params params; }

[[vk::binding(1, 1)]] Texture2D<float4>                        inputTex  : register(t1, space1);
[[vk::binding(2, 1)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> outputTex : register(u2, space1);
[[vk::binding(3, 1)]] cbuffer FlipCB : register(b3, space1)    { Flip flip; }

groupshared float3 tile[4][128];

[numthreads(32, 1, 1)]
void CSMain(uint3 WorkGroupID : SV_GroupID, uint3 LocalInvocationID : SV_GroupThreadID)
{
    int filterOffset = (params.filterDim - 1) / 2;

    uint width, height, mipLevels;
    inputTex.GetDimensions(0, width, height, mipLevels);
    int2 dims = int2(width, height);

    int2 baseIndex = int2(WorkGroupID.xy * uint2(params.blockDim, 4u) +
                          LocalInvocationID.xy * uint2(4u, 1u)) -
                     int2(filterOffset, 0);

    for (int r = 0; r < 4; r++)
    {
        for (int c = 0; c < 4; c++)
        {
            int2 loadIndex = baseIndex + int2(c, r);
            if (flip.value != 0u)
                loadIndex = loadIndex.yx;

            // Match the upstream imageBlur sample's gather coordinates.
            float2 uv = (float2(loadIndex) + float2(0.25f, 0.25f)) / float2(dims);
            tile[r][4 * LocalInvocationID.x + uint(c)] =
                inputTex.SampleLevel(samp, uv, 0.0f).rgb;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    for (int r2 = 0; r2 < 4; r2++)
    {
        for (int c2 = 0; c2 < 4; c2++)
        {
            int2 writeIndex = baseIndex + int2(c2, r2);
            if (flip.value != 0u)
                writeIndex = writeIndex.yx;

            int center = int(4u * LocalInvocationID.x) + c2;
            if (center >= filterOffset &&
                center < 128 - filterOffset &&
                all(writeIndex < dims))
            {
                float3 acc = float3(0.0f, 0.0f, 0.0f);
                for (int f = 0; f < params.filterDim; f++)
                {
                    int i = center + f - filterOffset;
                    acc += (1.0f / float(params.filterDim)) * tile[r2][i];
                }
                outputTex[writeIndex] = float4(acc, 1.0f);
            }
        }
    }
}
