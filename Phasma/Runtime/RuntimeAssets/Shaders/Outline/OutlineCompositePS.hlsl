#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

TexSamplerDecl(0, 0, Mask)
[[vk::push_constant]] PushConstants_SelectionOutline pc;

// The mask RT uses the default color-RT sampler, which is LINEAR + REPEAT. A ring-search read past
// the viewport border would wrap to the opposite edge (still covering the mesh) and draw a phantom
// outline there when a selection runs off-screen. Clamp UV to the texel-center band: at a texel
// center the bilinear weight on the wrapped neighbor is exactly zero, so this reads pure edge
// coverage (0 outside the silhouette) with no wrap contribution -- equivalent to clamp-to-edge.
// ponytail: in-shader clamp instead of a dedicated clamp sampler; only this pass reads the mask.
float SampleMask(float2 uv, float2 texel)
{
    return Mask.Sample(sampler_Mask, clamp(uv, 0.5 * texel, 1.0 - 0.5 * texel)).r;
}

// X-ray selection outline from an R8 coverage mask. A bounded ring search estimates the signed
// pixel distance to the silhouette edge; the line is solid out to `thickness` and smoothsteps off
// over `outerFade` outward / `innerFade` inward.
// ponytail: 16 dirs x reach px, early-rejected for the empty majority. Editor overlay over selected
// meshes only. Upgrade to a jump-flood SDF if very thick outlines ever get costly.
PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    PS_OUTPUT_Color output;

    float2 texel = pc.params0.xy;
    float thickness = pc.params0.z;
    float innerFade = pc.params0.w;
    float outerFade = pc.params1.x;

    float reach = thickness + max(innerFade, outerFade) + 1.0;
    float center = SampleMask(input.uv, texel);

    // Cheap reject: pixel is neither inside a selected silhouette nor within `reach` of one.
    float coarse = center;
    [unroll] for (int k = 0; k < 8; ++k)
    {
        float a = 0.78539816 * k; // 45 deg steps
        float2 d = float2(cos(a), sin(a)) * texel;
        coarse = max(coarse, SampleMask(input.uv + d * reach, texel));
        coarse = max(coarse, SampleMask(input.uv + d * (reach * 0.5), texel));
    }
    if (coarse < 0.5)
    {
        // No selected silhouette in reach: leave the frame untouched (do NOT write a
        // transparent texel — that would replace display if blend is disabled).
        discard;
        output.color = float4(0.0, 0.0, 0.0, 0.0);
        return output;
    }

    // Distance (px) to the nearest silhouette edge.
    bool inside = center > 0.5;
    float dist = reach;
    [loop] for (int dir = 0; dir < 16; ++dir)
    {
        float a = 0.39269908 * dir; // 22.5 deg steps
        float2 dirStep = float2(cos(a), sin(a)) * texel;
        [loop] for (float r = 1.0; r <= reach; r += 1.0)
        {
            float s = SampleMask(input.uv + dirStep * r, texel);
            if ((s > 0.5) != inside)
            {
                dist = min(dist, r);
                break;
            }
        }
    }

    float sd = inside ? -dist : dist; // <0 inside the object, >0 outside
    float alpha;
    if (sd >= 0.0)
        alpha = 1.0 - smoothstep(thickness, thickness + max(outerFade, 1e-3), sd);
    else
        alpha = 1.0 - smoothstep(0.0, max(innerFade, 1e-3), -sd);

    float finalAlpha = saturate(alpha) * pc.color.a;
    if (finalAlpha <= 0.0)
        discard;

    output.color = float4(pc.color.rgb, finalAlpha);
    return output;
}
