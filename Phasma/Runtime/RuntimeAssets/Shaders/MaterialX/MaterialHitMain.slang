// The hit side of MaterialHitHead.slang: rebuild the surface with the visibility path's reconstruction, then run the
// hit's shape. Skinned meshes and sprites are not in the TLAS, so every hit triangle is rigid.

// The instance's world transform comes from the TLAS (row-vector form), so the surface stays on the traced ray.
PeTriangle PeHitTriangle(uint instanceId)
{
    const uint3 indices = GetIndices(instanceId, PrimitiveIndex());
    const uint verticesOffset = meshInfos[instanceId].vertexOffset;
    const float4x3 m = ObjectToWorld4x3();

    PeTriangle t;
    t.v0 = PeLoadVertex(geometry, verticesOffset, indices.x);
    t.v1 = PeLoadVertex(geometry, verticesOffset, indices.y);
    t.v2 = PeLoadVertex(geometry, verticesOffset, indices.z);
    t.world = float4x4(float4(m[0], 0.0), float4(m[1], 0.0), float4(m[2], 0.0), float4(m[3], 1.0));
    t.c0 = float4(0.0, 0.0, 0.0, 0.0);
    t.c1 = float4(0.0, 0.0, 0.0, 0.0);
    t.c2 = float4(0.0, 0.0, 0.0, 0.0);
    t.deformed = false;

    return t;
}

// What subsurface's length(fwidth(N)) / length(fwidth(P)) measures at pixel scale, at triangle scale: the largest normal
// change per unit length along an edge (1/R for any triangle on a sphere of radius R with radial normals).
float PeHitCurvature(PeTriangle t)
{
    const float3x3 rot = float3x3(normalize(t.world[0].xyz), normalize(t.world[1].xyz), normalize(t.world[2].xyz));
    const float3 n0 = normalize(mul(t.v0.normal, rot));
    const float3 n1 = normalize(mul(t.v1.normal, rot));
    const float3 n2 = normalize(mul(t.v2.normal, rot));
    const float3 p0 = mul(float4(t.v0.position, 1.0), t.world).xyz;
    const float3 p1 = mul(float4(t.v1.position, 1.0), t.world).xyz;
    const float3 p2 = mul(float4(t.v2.position, 1.0), t.world).xyz;

    const float c01 = length(n1 - n0) / max(length(p1 - p0), 1e-6);
    const float c12 = length(n2 - n1) / max(length(p2 - p1), 1e-6);
    const float c20 = length(n0 - n2) / max(length(p0 - p2), 1e-6);

    return max(c01, max(c12, c20));
}

// The legacy hit's ambient occlusion: its occlusion texture, scaled by the material's occlusion strength.
float PeHitOcclusion(uint constantsId, float2 uv)
{
    if (!HasTexture(constants[constantsId].textureMask, TEX_OCCLUSION_BIT))
        return 1.0;

    return lerp(1.0, GetOcclusion(constantsId, uv, pe_hit_lod_base).r, LoadMaterial(constantsId).pbrParams.w);
}

// The ray cone's LOD at this hit (RT_RayConeLodBase) on the hit triangle's own UVs.
float PeHitLodBase(PeTriangle t, float coneWidth)
{
    return RT_RayConeLodBase(mul(float4(t.v0.position, 1.0), t.world).xyz,
                             mul(float4(t.v1.position, 1.0), t.world).xyz,
                             mul(float4(t.v2.position, 1.0), t.world).xyz,
                             t.v0.uv,
                             t.v1.uv,
                             t.v2.uv,
                             WorldRayDirection(),
                             coneWidth);
}

// The inverse of SrgbToLinearHdr: how the legacy emissive mirror stores a linear emission (brightness above 1 kept).
float3 PeLinearToSrgbHdr(float3 c)
{
    const float peak = max(max(c.r, c.g), max(c.b, 1.0));
    return LinearToSrgb(c / peak) * peak;
}

// GI and screen-probe rays light their hits as the legacy surface cache does (RT_FoldSurfaceCache): the graph's own inputs,
// diffuse only, so a glossy MaterialX surface never reaches the probes as fireflies. In the legacy hit's colour space:
// linear under linear_color, authored (sRGB) values otherwise.
float3 PeSurfaceCacheHit(PeSurfaceInputs p)
{
    float3 albedo = saturate(p.base_color * p.base_weight);
    float metallic = saturate(p.base_metalness);
    float roughness = saturate(p.specular_roughness);
    float3 emission = max(p.emission_color * p.emission_luminance, 0.0);
    if (cb_linearColor == 0)
    {
        albedo = LinearToSrgb(albedo);
        emission = PeLinearToSrgbHdr(emission);
    }

    const float3 N = normalize(p.geometry_normal);
    const float3 V = normalize(-WorldRayDirection());
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    RT_FoldSurfaceCache(albedo, metallic, roughness, F0);

    const float NdV = clamp(dot(N, V), 0.001, 1.0);
    float2 envBRDF;
    const float3 energyCompensation = RT_EnergyCompensation(NdV, roughness, F0, envBRDF);

    return RT_LightOpaque(pe_P,
                          pe_P + pe_shadow_offset,
                          N,
                          V,
                          albedo,
                          metallic,
                          roughness,
                          F0,
                          pe_occlusion,
                          envBRDF,
                          energyCompensation) +
           emission;
}

// A forward mesh's render types (MeshUsesOpenPBRForward): MeshInfoGPU::renderType 3 alpha-blend, 4 Transmission.
bool PeBlendedRenderType(uint renderType)
{
    return renderType == 3u || renderType == 4u;
}

#ifndef GI_PROBES
// Leaving a Transmission mesh (its entry hit already shaded and tinted the light): the ray only refracts out, as the legacy
// hit's exit faces pass the escaped radiance through. N is the surface normal, out of the medium.
float3 PeExitTransmission(float ior, float3 N)
{
    if (pe_hit_depth >= 2u)
        return PeSkyAlongRay();

    const float3 d = normalize(WorldRayDirection());
    float3 dir = refract(d, -N, max(ior, 1e-3));
    if (!(dot(dir, dir) > 0.0))
        dir = reflect(d, -N); // total internal reflection

    return PeTraceContinuation(normalize(dir), true);
}

// What an alpha-blend surface lets through, in the hit output's space.
float3 PeBehindSurface()
{
    return pe_hit_depth < 2u ? PeTraceContinuation(normalize(WorldRayDirection()), false) : PeSkyAlongRay();
}
#endif

[shader("closesthit")]
void pe_hit_closest(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    const uint instanceId = InstanceID();
    const uint constantsId = meshInfos[instanceId].constantsIndex;
    const uint shape = PeHitShape(constantsId);
    const PeTriangle tri = PeHitTriangle(instanceId);
    const float3 b = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);

    PeSurfacePoint sp = PeSurfaceAtBarycentrics(tri, b);
    PeBendLimbNormal(tri, -normalize(WorldRayDirection()), sp);

    pe_P = sp.P;
    pe_N = sp.N;
    pe_spec_N = sp.N;
    pe_hit_depth = payload.depth;
    pe_hit_cone_width = RT_ConeWidthAtHit(payload);
    pe_hit_cone_spread = payload.coneSpread;
    pe_hit_refracts = false;
    pe_refraction_traced = false;
    pe_hit_lod_base = PeHitLodBase(tri, pe_hit_cone_width);
    pe_occlusion = PeHitOcclusion(constantsId, sp.uv0);
    pe_hit_curvature = PeHitCurvature(tri);

    const float3 terminatorOffset = ShadowTerminatorOffset(tri.v0.position,
                                                           tri.v1.position,
                                                           tri.v2.position,
                                                           tri.v0.normal,
                                                           tri.v1.normal,
                                                           tri.v2.normal,
                                                           b);
    pe_shadow_offset = mul(float4(terminatorOffset, 0.0), tri.world).xyz;

    // The graph's inputs, then the BSDF outside the shape's case: the area-light seam sees the graph's normal and the BSDF is
    // inlined once, not in every shape's case (DX12 inlines the dispatchers).
    const uint offset = constants[constantsId].materialByteOffset + PE_OPENPBR_INPUTS_OFFSET;
    const PeSurfaceInputs inputs = pe_material_inputs(shape, pe_materialBytes, offset, sp, sp.nextUv, sp.spriteBlend);
#if defined(GI_PROBES) && !defined(REFLECTIONS)
    const float3 color = PeSurfaceCacheHit(inputs);
#else
    const float3 shadingN = inputs.geometry_normal;
    if (dot(shadingN, shadingN) > 1e-8)
        pe_spec_N = normalize(shadingN);

#ifndef GI_PROBES
    // Forward meshes reach only the camera pass's hits (probe rays skip them in pe_hit_any).
    const uint renderType = meshInfos[instanceId].renderType;
    const bool blended = PeBlendedRenderType(renderType);
    if (renderType == 4u && dot(WorldRayDirection(), sp.N) > 0.0)
    {
        const float3 escaped = PeExitTransmission(inputs.specular_ior, sp.N);
        payload.radiance = any(isnan(escaped)) || any(isinf(escaped)) ? float3(0.0, 0.0, 0.0) : escaped;
        payload.t = RayTCurrent();
        payload.hitKind = HitKind();
        payload.alpha = 1.0;
        return;
    }

    pe_hit_refracts = blended && payload.depth < 2u;
#endif

    float3 color = pe_shade_inputs(inputs).rgb;

    // As material pass 2: scene-linear under linear_color, display-referred otherwise (as legacy hits are then).
    if (cb_linearColor == 0)
        color = LinearToSrgb(color);

#ifndef GI_PROBES
    // The forward pass's blend (MaterialX's SRC_ALPHA over a colour that already carries the opacity), over what the
    // surface lets through.
    const float alpha = saturate(inputs.geometry_opacity);
    if (blended && alpha < 1.0)
        color = color * alpha + PeBehindSurface() * (1.0 - alpha);
#endif
#endif

    payload.radiance = any(isnan(color)) || any(isinf(color)) ? float3(0.0, 0.0, 0.0) : color;
    payload.t = RayTCurrent();
    payload.hitKind = HitKind();
    payload.alpha = 1.0;
}

// Alpha-cut MaterialX meshes: the graph's own opacity decides what rays see, with material pass coverage's rule
// (MaterialMaskMain.slang). Shadow rays through a covered but partly transparent texel keep the legacy hit's tint.
// Forward meshes follow the legacy any-hit's rules for blended surfaces, with the graph's opacity: probe rays skip them,
// Transmission meshes tint shadows, alpha-blend ones let 1 - opacity through.
[shader("anyhit")]
void pe_hit_any(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    const uint instanceId = InstanceID();
    const uint constantsId = meshInfos[instanceId].constantsIndex;
    const uint renderType = meshInfos[instanceId].renderType;
    const bool blended = PeBlendedRenderType(renderType);
    if (blended && payload.skipBlended != 0u)
    {
        IgnoreHit();
        return;
    }

    if (renderType == 4u && payload.rayType == 1u)
    {
        payload.radiance *= LoadMaterial(constantsId).shadowTransmittance.rgb; // the raster tint maps' value
        IgnoreHit();
        return;
    }

    const uint shape = PeHitShape(constantsId);
    const PeTriangle tri = PeHitTriangle(instanceId);
    const float3 b = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    const PeSurfacePoint sp = PeSurfaceAtBarycentrics(tri, b);

    pe_P = sp.P;
    pe_N = sp.N;
    pe_hit_lod_base = -1000.0; // coverage at mip 0, as the legacy any-hit tests alpha
    const uint offset = constants[constantsId].materialByteOffset + PE_OPENPBR_INPUTS_OFFSET;
    const float opacity = pe_material_mask(shape, pe_materialBytes, offset, sp, sp.nextUv, sp.spriteBlend);
    if (blended)
    {
        if (!(opacity >= 0.001)) // MaterialX's transparent-surface threshold, as the forward pass discards
        {
            IgnoreHit();
            return;
        }

        if (renderType == 3u && payload.rayType == 1u && opacity < 1.0)
        {
            payload.radiance *= 1.0 - opacity;
            IgnoreHit();
        }

        return;
    }

    if (!(opacity > 0.0 && opacity >= constants[constantsId].alphaCut))
    {
        IgnoreHit();
        return;
    }

    if (payload.rayType == 1 && opacity < 1.0)
    {
        payload.radiance *= 1.0 - opacity;
        IgnoreHit();
    }
}
