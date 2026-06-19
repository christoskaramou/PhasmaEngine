// Port of webgpu-samples/alpha-to-coverage-emulator ShowMultisampleTexture.wgsl
// (fragment side) to HLSL (SM 6.0).
//
// Renders a visualization that:
//   - Shows the resolved texture as the background when the visualization
//     resolution is too high to draw sample-site markers.
//   - Overlays a grid with per-pixel cells when zoomed in.
//   - Renders a circle per sample site colored by textureLoad(texLargeDot,
//     xyInt, sampleIndex).
//   - Renders a smaller dot next to each sample site showing the value of
//     the comparison texture (texSmallDot).

Texture2DMS<float4> texLargeDot : register(t0, space0);
Texture2DMS<float4> texSmallDot : register(t1, space0);
Texture2D<float4>   resolved    : register(t2, space0);

struct Varying
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

static const int    kSampleCount = 4;
static const float2 kSamplePositions[4] = {
    float2(0.375f, 0.125f),
    float2(0.875f, 0.375f),
    float2(0.125f, 0.625f),
    float2(0.625f, 0.875f),
};

static const float  kSampleDistanceFromCloseEdge = 0.125f;
static const float  kGridEdgeHalfWidth           = 0.025f;
static const float  kSampleInnerRadius = 0.1f;   // 0.125 - 0.025
static const float  kSampleOuterRadius = 0.15f;  // 0.125 + 0.025
static const float4 kGridColor = float4(0.1f, 0.1f, 0.1f, 1.0f);

float4 PSMain(Varying vary) : SV_Target0
{
    uint2 dim;
    uint  samples;
    texLargeDot.GetDimensions(dim.x, dim.y, samples);
    float dimMax = (float)max(dim.x, dim.y);

    float2 xy     = vary.uv * dimMax;
    int2   xyInt  = int2(xy);
    float2 xyFrac = fmod(xy, float2(1.0f, 1.0f));

    // Only draw the grid/sample overlay if the visualization resolution is
    // large enough to see it (matches upstream dpdx/dpdy check).
    bool showOverlay =
        (ddx(xy.x) < (kGridEdgeHalfWidth * 2.0f)) &&
        (ddy(xy.y) < (kGridEdgeHalfWidth * 2.0f));

    if (showOverlay)
    {
        [unroll]
        for (int sampleIndex = 0; sampleIndex < kSampleCount; ++sampleIndex)
        {
            float2 center = kSamplePositions[sampleIndex];
            float  distanceFromCenter = distance(xyFrac, center);
            float  distanceFromComparisonCenter =
                distance(xyFrac, center + float2(kSampleInnerRadius * 0.5f, 0.0f));

            if (distanceFromCenter < kSampleOuterRadius)
            {
                if (distanceFromCenter > kSampleInnerRadius)
                {
                    // Ring around the circle.
                    return kGridColor;
                }
                else if (distanceFromComparisonCenter < (kSampleInnerRadius * 0.5f))
                {
                    // Comparison sample dot on the right.
                    float3 val = texSmallDot.Load(xyInt, sampleIndex).rgb;
                    return float4(val, 1.0f);
                }
                else if (distanceFromCenter < kSampleInnerRadius)
                {
                    // Full circle for the sample value.
                    float3 val = texLargeDot.Load(xyInt, sampleIndex).rgb;
                    return float4(val, 1.0f);
                }
            }
        }

        float2 distanceToGridEdge = abs(fmod(xyFrac + 0.5f, 1.0f) - 0.5f);
        if (min(distanceToGridEdge.x, distanceToGridEdge.y) < kGridEdgeHalfWidth)
        {
            return kGridColor;
        }
    }

    float3 bg = resolved.Load(int3(xyInt, 0)).rgb;
    return float4(bg, 1.0f);
}
