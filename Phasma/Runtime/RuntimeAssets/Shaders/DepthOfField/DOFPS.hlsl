#include "../Common/Structures.hlsl"
#include "DOF.hlsl"

[[vk::push_constant]] ConstantBuffer<DOFParams> pc;

[[vk::binding(0, 0)]] Texture2D<float4> Packed; // colour, inverse depth (DOFPrepassCS)
[[vk::binding(1, 0)]] Texture2D<float> Reach;   // per tile, the largest blur that can reach it (DOFTileDilateCS)

static const float GOLDEN_ANGLE = 2.39996323;
static const float INNER_STEP = 0.5;   // dense spiral out to INNER_RADIUS: blurs of a few pixels are sampled finely
static const float INNER_RADIUS = 6.0;
static const uint OUTER_SAMPLES = 160; // equal-area spiral from INNER_RADIUS to the tile's reach

struct Gathered
{
    float3 color;
    float total;
};

void Gather(inout Gathered g, float4 tap, float centreInverse, float centreRadius, float radius, float weight,
            float inverseFocus, float scale)
{
    float tapRadius = BlurRadius(tap.a, inverseFocus, scale, pc);

    // A sample behind this pixel spreads over it no further than the pixel's own blur, so a sharp silhouette keeps
    // its edge instead of taking on the blurred background behind it.
    if (tap.a < centreInverse)
        tapRadius = min(tapRadius, centreRadius * 2.0);

    const float m = smoothstep(radius - 0.5, radius + 0.5, tapRadius);
    g.color += weight * lerp(g.color / g.total, tap.rgb, m);
    g.total += weight;
}

float4 Tap(int2 pixel, float angle, float radius, int2 last)
{
    const int2 texel = int2(round(float2(pixel) + float2(cos(angle), sin(angle)) * radius));
    return Packed.Load(int3(clamp(texel, int2(0, 0), last), 0));
}

float3 DepthOfField(int2 pixel, float4 centre, int2 size)
{
    if ((pc.flags & DOF_PERSPECTIVE) == 0u) // an orthographic camera has no lens
        return centre.rgb;

    // Half a pixel past the largest blur in range: the smoothstep edge of its disk still counts.
    const float reach = Reach.Load(int3(pixel / DOF_TILE, 0)) + 0.5;
    const float inverseFocus = InverseFocus(Packed.Load(int3(size / 2, 0)).a, pc);
    const float scale = BlurPerInverseDepth(inverseFocus, pc);
    const float centreRadius = BlurRadius(centre.a, inverseFocus, scale, pc);
    const int2 last = size - 1;

    Gathered g;
    g.total = INNER_STEP;
    g.color = centre.rgb * g.total;

    // Equal-area spirals (r^2 grows by 2 * step per sample), each sample weighted by the area it stands for, so the
    // two densities add up to a uniform disk; the outer one spreads its samples over this tile's reach only.
    float angle = 0.0;
    float radius = INNER_STEP;
    const float innerEnd = min(INNER_RADIUS, reach);
    while (radius < innerEnd)
    {
        Gather(g, Tap(pixel, angle, radius, last), centre.a, centreRadius, radius, INNER_STEP, inverseFocus, scale);
        radius += INNER_STEP / radius;
        angle += GOLDEN_ANGLE;
    }

    const float outerStep = max(INNER_STEP, (reach * reach - innerEnd * innerEnd) / (2.0 * OUTER_SAMPLES));
    for (uint i = 0; i < OUTER_SAMPLES && radius < reach; ++i)
    {
        Gather(g, Tap(pixel, angle, radius, last), centre.a, centreRadius, radius, outerStep, inverseFocus, scale);
        radius += outerStep / radius;
        angle += GOLDEN_ANGLE;
    }

    return g.color / g.total;
}

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    uint width, height;
    Packed.GetDimensions(width, height);
    const int2 size = int2(width, height);
    const int2 pixel = min(int2(input.uv * float2(size)), size - 1);
    const float4 centre = Packed.Load(int3(pixel, 0));

    // The pipeline writes colour only: the display target keeps its alpha.
    PS_OUTPUT_Color output;
    output.color = float4(lerp(centre.rgb, DepthOfField(pixel, centre, size), saturate(pc.blend)), 1.0);

    return output;
}
