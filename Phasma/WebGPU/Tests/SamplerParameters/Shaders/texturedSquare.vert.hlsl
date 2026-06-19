struct Config
{
    column_major float4x4 viewProj;
    float2 animationOffset;
    float flangeSize;
    float highlightFlange;
};

[[vk::binding(0, 0)]] cbuffer ConfigCB : register(b0, space0)
{
    Config config;
}

[[vk::binding(1, 0)]] StructuredBuffer<column_major float4x4> matrices : register(t1, space0);

struct VSOutput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

// Upstream @override constants inlined (kTextureBaseSize=16, kViewportSize=48).
static const float kTextureBaseSize = 16.0;
static const float kViewportSize = 48.0;

VSOutput VSMain(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    float flange = config.flangeSize;

    float2 uvs[6] = {
        float2(-flange, -flange),
        float2(-flange, 1.0 + flange),
        float2(1.0 + flange, -flange),
        float2(1.0 + flange, -flange),
        float2(-flange, 1.0 + flange),
        float2(1.0 + flange, 1.0 + flange),
    };

    // At identity the square is sized so 1 texel == 1 pixel in the 48-px cell.
    float radius = (1.0 + 2.0 * flange) * kTextureBaseSize / kViewportSize;
    float2 positions[6] = {
        float2(-radius, -radius),
        float2(-radius, radius),
        float2(radius, -radius),
        float2(radius, -radius),
        float2(-radius, radius),
        float2(radius, radius),
    };

    float4x4 model = matrices[iid];
    float4 worldPos = mul(model, float4(positions[vid] + config.animationOffset, 0.0, 1.0));

    VSOutput o;
    o.svPos = mul(config.viewProj, worldPos);
    o.uv = uvs[vid];
    return o;
}
