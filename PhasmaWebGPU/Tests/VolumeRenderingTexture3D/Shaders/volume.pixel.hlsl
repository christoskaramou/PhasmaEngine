[[vk::binding(1, 0)]] SamplerState mySampler : register(s1, space0);
[[vk::binding(2, 0)]] Texture3D<float> myTexture : register(t2, space0);

struct PSInput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float3 near : TEXCOORD0;
    [[vk::location(1)]] float3 step : TEXCOORD1;
};

static const uint kNumSteps = 64u;

// Direct volume rendering via ray marching through a normalized [-1, 1]^3
// volume. Mirrors upstream volume.wgsl fragment_main() exactly.
float4 PSMain(PSInput input) : SV_Target0
{
    float3 rayPos = input.near;
    float result = 0.0f;

    [loop] for (uint i = 0u; i < kNumSteps; i++)
    {
        float3 texCoord = (rayPos + 1.0f) * 0.5f;
        float sampleValue =
            myTexture.SampleLevel(mySampler, texCoord, 0.0f) * 4.0f / (float)kNumSteps;

        bool intersects = all(rayPos < float3(1.0f, 1.0f, 1.0f)) &&
                          all(rayPos > float3(-1.0f, -1.0f, -1.0f));
        if (intersects && result < 1.0f)
        {
            result += (1.0f - result) * sampleValue;
        }
        rayPos += input.step;
    }

    return float4(result, result, result, 1.0f);
}
