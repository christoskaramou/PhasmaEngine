// Shared fragment shader for both culled and non-culled paths. Matches
// upstream GEOMETRY_SHADER fragmentMain exactly (Lambert with hardcoded
// directional light + ambient).

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

struct VSOut
{
    float4 svPos : SV_Position;
    float3 norm : NORMAL;
    float2 uv0 : TEXCOORD0;
};

float4 PSMain(VSOut input) : SV_Target0
{
    const float3 lightDir = float3(0.25, 0.5, 1.0);
    const float3 lightColor = float3(1.0, 1.0, 1.0);
    const float3 ambientColor = float3(0.03, 0.03, 0.03);

    float4 baseColor = material.color;
    float3 N = normalize(input.norm);
    float3 L = normalize(lightDir);
    float NDotL = max(dot(N, L), 0.0);
    float3 surfaceColor = baseColor.rgb * ambientColor + baseColor.rgb * NDotL;
    return float4(surfaceColor, baseColor.a);
}
