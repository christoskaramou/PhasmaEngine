struct VSOutput
{
    float4 svPos : SV_Position;
};

VSOutput VSMain(uint vid : SV_VertexID)
{
    float2 pos[3] = {
        float2(0.0f, 0.5f),
        float2(-0.5f, -0.5f),
        float2(0.5f, -0.5f)
    };

    VSOutput o;
    // Flip Y for Vulkan Y-down NDC (WebGPU NDC is Y-up).
    o.svPos = float4(pos[vid].x, -pos[vid].y, 0.0f, 1.0f);
    return o;
}
