#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

static const int MAX_DATA_SIZE = 2048;
[[vk::binding(0)]] tbuffer UBO { float4x4 data[MAX_DATA_SIZE]; };
TexSamplerDecl(1, 0, Depth)

float4x4 GetViewProjection() { return data[0]; }
float4x4 GetInvView()        { return data[2]; }
float4x4 GetInvProjection()  { return data[3]; }

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    float4x4 invView = GetInvView();
    float3 cameraPos = float3(invView[3][0], invView[3][1], invView[3][2]);

    float4x4 invViewProj = mul(GetInvProjection(), invView);

    // Build view ray
    float3 pNear = GetPosFromUV(input.uv, 1.0, invViewProj);
    float3 pFar  = GetPosFromUV(input.uv, 0.0, invViewProj);
    float3 dir   = normalize(pFar - pNear);

    // Ray-plane intersection (y = 0)
    if (abs(dir.y) < 1e-4)
        discard;

    float t = (0.0 - cameraPos.y) / dir.y;
    if (t <= 0.0)
        discard;

    float3 worldPos = cameraPos + dir * t;

    // Manual depth test against scene depth
    float sceneDepth = Depth.Sample(sampler_Depth, input.uv).x;
    float3 scenePos = GetPosFromUV(input.uv, sceneDepth, invViewProj);
    float tScene = dot(scenePos - cameraPos, dir);
    if (t > tScene + 0.01)
        discard;

    // Analytic grid with fwidth AA
    const float gridSpacing = 1.0;
    float2 coord = worldPos.xz / gridSpacing;
    float2 grid = abs(frac(coord - 0.5) - 0.5) / fwidth(coord);
    float lineValue = min(grid.x, grid.y);
    float alpha = 1.0 - saturate(lineValue);

    // Axis emphasis
    float axisX = 1.0 - smoothstep(0.0, fwidth(worldPos.x) * 2.0, abs(worldPos.x));
    float axisZ = 1.0 - smoothstep(0.0, fwidth(worldPos.z) * 2.0, abs(worldPos.z));

    float3 color = float3(0.5, 0.5, 0.5);
    color = lerp(color, float3(1.0, 0.0, 0.0), axisZ); // Z=0 -> Red X axis
    color = lerp(color, float3(0.0, 0.0, 1.0), axisX); // X=0 -> Blue Z axis

    // Distance fade
    float d = distance(cameraPos, worldPos);
    float fadeDistance = 400.0;
    float fadeStart = 100.0;
    alpha *= 1.0 - smoothstep(fadeStart, fadeDistance, d);

    if (alpha <= 0.001)
        discard;

    PS_OUTPUT_Color output;
    output.color = float4(color, alpha);
    return output;
}
