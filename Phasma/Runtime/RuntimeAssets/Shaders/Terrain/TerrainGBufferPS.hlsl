// TerrainGBufferPS.hlsl — dedicated GBuffer pixel shader for the streamed isosurface terrain.
//
// Terrain tiles are STANDARD float meshes (stock GBufferVS feeds this PS), so world position and
// world normal arrive interpolated. This shader replaces the terrain's flat per-vertex colour bands
// with triplanar-sampled material layers blended by a splat map:
//   - 4 layers (0 grass, 1 rock, 2 sand, 3 snow) sampled triplanar (3 world planes blended by the
//     normal) so cliffs never stretch. Loaded UNORM (one Texture2D each), linearized in-shader.
//   - A splat map (RGBA = grass/rock/sand/snow weights) overrides where painted; unpainted texels
//     (weight sum ~0) fall back to an auto height/slope selection, so terrain is textured with no
//     painting and streamed regions outside the splat map still look right.
// The mesher packs the normalized surface height into color.a: scatter props get < 0.5 (this shader
// keeps their baked colour), ground gets 0.5 + 0.5*f so the auto height selection needs no per-draw
// constants. color.rgb is a tint carrying the cave-interior and underwater shading.

#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

[[vk::push_constant]] PushConstants_GBuffer pc;
[[vk::binding(0, 1)]] SamplerState terrain_sampler : register(s0, space1);
[[vk::binding(1, 1)]] Texture2D<float4> gTerrainSplat : register(t1, space1);
[[vk::binding(2, 1)]] Texture2D<float4> gLayer0 : register(t2, space1); // grass
[[vk::binding(3, 1)]] Texture2D<float4> gLayer1 : register(t3, space1); // rock
[[vk::binding(4, 1)]] Texture2D<float4> gLayer2 : register(t4, space1); // sand
[[vk::binding(5, 1)]] Texture2D<float4> gLayer3 : register(t5, space1); // snow
// Optional per-layer material maps (RGB = tangent-space normal, A = roughness), same layer order.
// A flat-normal default (RGB 0.5,0.5,1.0 + A 1.0) is bound where a layer has no map, so the whiteout
// blend below reproduces the geometric normal + full roughness exactly — no visual change until a
// map is supplied, and the default 1x1 texels are cache-resident (no bandwidth cost).
[[vk::binding(6, 1)]] Texture2D<float4> gMat0 : register(t6, space1);
[[vk::binding(7, 1)]] Texture2D<float4> gMat1 : register(t7, space1);
[[vk::binding(8, 1)]] Texture2D<float4> gMat2 : register(t8, space1);
[[vk::binding(9, 1)]] Texture2D<float4> gMat3 : register(t9, space1);

// Layers are UNORM (DX12 forbids SRGB storage images), so linearize in-shader before lighting.
float3 ToLinear(float3 c) { return pow(c, 2.2f); }

// One layer sampled triplanar: project onto the three world planes, blend by the sharpened normal.
// s = 1 / (metres per tile), so bigger tiles sample lower texture frequency.
float3 TriplanarLayer(Texture2D<float4> tex, float3 wp, float3 blend, float s)
{
    float3 cx = ToLinear(tex.Sample(terrain_sampler, wp.zy * s).rgb);
    float3 cy = ToLinear(tex.Sample(terrain_sampler, wp.xz * s).rgb);
    float3 cz = ToLinear(tex.Sample(terrain_sampler, wp.xy * s).rgb);
    return cx * blend.x + cy * blend.y + cz * blend.z;
}

// One layer's material sampled triplanar. Normal maps are linear (NOT sRGB — no ToLinear): unpack
// each plane's tangent normal, reorient into world space via the Whiteout blend (no per-vertex
// tangents needed), and triblend by the same sharpened-normal weights. Returns the (un-normalized)
// world normal in .xyz and the triplanar-blended roughness in .w.
float4 TriplanarMaterial(Texture2D<float4> tex, float3 wp, float3 N, float3 blend, float s)
{
    float4 mx = tex.Sample(terrain_sampler, wp.zy * s);
    float4 my = tex.Sample(terrain_sampler, wp.xz * s);
    float4 mz = tex.Sample(terrain_sampler, wp.xy * s);
    float3 tX = mx.xyz * 2.0f - 1.0f;
    float3 tY = my.xyz * 2.0f - 1.0f;
    float3 tZ = mz.xyz * 2.0f - 1.0f;
    // Whiteout blend (Golus): fold the geometric normal into each plane's tangent normal, then
    // swizzle each back to world orientation. A flat (0,0,1) tangent normal collapses to N exactly.
    tX = float3(tX.xy + N.zy, abs(tX.z) * N.x);
    tY = float3(tY.xy + N.xz, abs(tY.z) * N.y);
    tZ = float3(tZ.xy + N.xy, abs(tZ.z) * N.z);
    float3 wn = tX.zyx * blend.x + tY.xzy * blend.y + tZ.xyz * blend.z;
    float rough = mx.w * blend.x + my.w * blend.y + mz.w * blend.z;
    return float4(wn, rough);
}

PS_OUTPUT_Gbuffer mainPS(PS_INPUT_Gbuffer input)
{
    PS_OUTPUT_Gbuffer output;

    float3 N = normalize(input.normal);
    float3 albedo;
    float3 outN = N;         // surface normal written to the gbuffer (perturbed for ground below)
    float roughness = 1.0f;  // matches the terrain's prior fixed max-roughness output

    // Scatter props (trees/rocks/grass) are baked into the tile with color.a < 0.5 — keep their
    // baked colour, they are not ground.
    if (input.color.a < 0.5f)
    {
        albedo = input.color.rgb;
    }
    else
    {
        const float3 wp = input.positionWS.xyz;
        const float f = saturate((input.color.a - 0.5f) * 2.0f); // normalized surface height
        const float slope = 1.0f - saturate(N.y);

        // Auto weights from height + slope (grass, rock, sand, snow) — the unpainted default.
        float4 w;
        w.z = 1.0f - smoothstep(0.02f, 0.14f, f);   // sand: low ground
        w.w = smoothstep(0.72f, 0.95f, f);           // snow: high ground
        w.x = saturate(1.0f - w.z - w.w);            // grass: the middle band
        w.y = smoothstep(0.45f, 0.75f, slope);       // rock: steep faces, any height
        w.x *= (1.0f - w.y);                         // rock takes over the others on slopes
        w.z *= (1.0f - w.y);
        w.w *= (1.0f - w.y);

        // Painted splat overrides the auto selection where its weight sum is non-trivial. saturate:
        // the shared sampler is REPEAT (layers tile), but the splat is an authored per-world map that
        // must edge-clamp — tiles straddling/outside the footprint have uv beyond [0,1] and would
        // otherwise wrap in weights from the opposite edge.
        const float4 sp = gTerrainSplat.Sample(terrain_sampler, saturate(input.uv));
        const float spSum = sp.x + sp.y + sp.z + sp.w;
        float4 weight = spSum > 0.01f ? sp : w;
        weight /= (weight.x + weight.y + weight.z + weight.w + 1e-4f);

        float3 blend = pow(abs(N), 4.0f);            // sharpen so faces read as one plane, not a smear
        blend /= (blend.x + blend.y + blend.z + 1e-4f);

        // Configurable metres per tile (pc.terrainTexScale, default 3 m if unset). s = 1 / metres.
        const float s = 1.0f / (pc.terrainTexScale > 0.01f ? pc.terrainTexScale : 3.0f);
        albedo = weight.x * TriplanarLayer(gLayer0, wp, blend, s) +
                 weight.y * TriplanarLayer(gLayer1, wp, blend, s) +
                 weight.z * TriplanarLayer(gLayer2, wp, blend, s) +
                 weight.w * TriplanarLayer(gLayer3, wp, blend, s);

        // Per-layer normal + roughness, weight-blended like the albedo. Flat default maps leave
        // outN == N and roughness == 1, so unauthored terrain is byte-identical to before.
        float4 mat = weight.x * TriplanarMaterial(gMat0, wp, N, blend, s) +
                     weight.y * TriplanarMaterial(gMat1, wp, N, blend, s) +
                     weight.z * TriplanarMaterial(gMat2, wp, N, blend, s) +
                     weight.w * TriplanarMaterial(gMat3, wp, N, blend, s);
        outN = normalize(mat.xyz);
        roughness = mat.w;

        // Per-vertex tint carries the cave-interior rock darkening and the underwater blue mix.
        albedo *= input.color.rgb;
    }

    output.normal = float4(outN * 0.5f + 0.5f, 1.0f);
    output.albedo = float4(albedo, 1.0f);
    output.metRough = float4(1.0f, roughness, 0.0f, 0.0f); // full occlusion, per-layer roughness, no metal
    output.emissive = float4(0.0f, 0.0f, 0.0f, 1.0f);
    output.transparency = pc.passType ? 1.0f : 0.0f;

    // Velocity (matches stock GBuffer PS).
    float2 currentNDC = input.positionCS.xy / input.positionCS.w;
    float2 previousNDC = input.prevPositionCS.xy / input.prevPositionCS.w;
    currentNDC -= float2(pc.projJitter.x, pc.projJitter.y);
    previousNDC -= float2(pc.prevProjJitter.x, pc.prevProjJitter.y);
    output.velocity = previousNDC - currentNDC;

    return output;
}
