#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

[[vk::binding(0)]] ByteAddressBuffer data;
TexSamplerDecl(1, 0, Depth)
[[vk::push_constant]] PushConstants_Grid pc;

float4x4 LoadMatrix(uint matrixIndex)
{
    uint offset = matrixIndex * 64u;
    float4x4 result;
    result[0] = asfloat(data.Load4(offset));
    result[1] = asfloat(data.Load4(offset + 16));
    result[2] = asfloat(data.Load4(offset + 32));
    result[3] = asfloat(data.Load4(offset + 48));
    return result;
}

float GridCoverage(float2 position, float2 footprint, float spacing)
{
    float2 distanceInPixels = abs(frac(position / spacing + 0.5) - 0.5) * spacing / footprint;
    float2 coverage = 1.0 - smoothstep(0.0, 1.0, distanceInPixels);
    // Remove each family of lines before its cells become smaller than a few pixels.
    coverage *= 1.0 - smoothstep(0.125, 0.25, footprint / spacing);
    return max(coverage.x, coverage.y);
}

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    float4x4 invView = LoadMatrix(2);
    float4x4 invProjection = LoadMatrix(3);
    // The overlay is unjittered; depth remains in the jittered, possibly lower-resolution scene.
    float2 sceneUV = NdcToUv(UvToNdc(input.uv) + pc.projJitter);
    float3 nearView = GetPosFromUV(sceneUV, 1.0, invProjection);
    float3 midView = GetPosFromUV(sceneUV, 0.5, invProjection);
    float3 viewDirection = normalize(midView - nearView);
    float3 origin = mul(float4(nearView, 1.0), invView).xyz;
    float3 direction = mul(float4(viewDirection, 0.0), invView).xyz;

    // Starting at the near-plane point supports parallel orthographic rays as well as perspective.
    // Both unprojection depths are finite, including with an infinite reverse-Z far plane.
    float denominator = abs(direction.y) < 1e-6 ? (direction.y < 0.0 ? -1e-6 : 1e-6) : direction.y;
    float t = -origin.y / denominator;
    float2 position = (origin + direction * t).xz;
    float2 dx = ddx(position);
    float2 dy = ddy(position);
    float2 footprint = max(sqrt(dx * dx + dy * dy), 1e-6);

    // Derivatives must be evaluated before any depth/plane-dependent discard.
    if (t < 0.0 || abs(direction.y) < 1e-6 || !all(isfinite(position)) || !all(isfinite(footprint)))
        discard;

    uint depthWidth, depthHeight;
    Depth.GetDimensions(depthWidth, depthHeight);
    int2 depthPixel = clamp(int2(sceneUV * float2(depthWidth, depthHeight)), int2(0, 0), int2(depthWidth, depthHeight) - 1);
    float sceneDepth = Depth.Load(int3(depthPixel, 0)).x;
    // Zero depth is sky, not a distant surface that can clip the grid.
    if (sceneDepth > 0.0)
    {
        float3 sceneView = GetPosFromUV(sceneUV, sceneDepth, invProjection);
        if (t > dot(sceneView - nearView, viewDirection) + 0.002)
            discard;
    }

    // World-aligned decades cross-fade continuously. A third level preserves major-line
    // emphasis when one decade becomes the next, without a pop at integer LOD boundaries.
    float lod = max(0.0, log10(max(footprint.x, footprint.y) * 16.0));
    float spacing = pow(10.0, floor(lod));
    float blend = frac(lod);
    float fine = GridCoverage(position, footprint, spacing) * 0.28 * (1.0 - blend);
    float medium = GridCoverage(position, footprint, spacing * 10.0) * lerp(0.55, 0.28, blend);
    float coarse = GridCoverage(position, footprint, spacing * 100.0) * 0.55;
    float alpha = max(fine, max(medium, coarse));

    float2 axes = 1.0 - smoothstep(0.25, 1.25, abs(position) / footprint);
    float3 color = float3(0.42, 0.42, 0.42);
    color = lerp(color, float3(0.85, 0.12, 0.10), axes.y);
    color = lerp(color, float3(0.12, 0.25, 0.90), axes.x);
    alpha = max(alpha, max(axes.x, axes.y) * 0.8);
    // Fade only at grazing angles, never at a finite radius around the camera.
    alpha *= smoothstep(0.005, 0.04, abs(direction.y));

    PS_OUTPUT_Color output;
    output.color = float4(color, alpha);
    return output;
}
