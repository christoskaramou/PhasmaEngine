// The hit side of MaterialHitHead.slang: rebuild the surface with the visibility path's reconstruction, then run the
// module. Skinned meshes and sprites are not in the TLAS, so every hit triangle is rigid.

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

    return lerp(1.0, GetOcclusion(constantsId, uv).r, LoadMaterial(constantsId).pbrParams.w);
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
float3 PeSurfaceCacheHit(PeSurfacePoint sp)
{
    const PeSurfaceInputs p = pe_material_inputs(sp.nextUv, sp.spriteBlend);
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

[shader("closesthit")]
void pe_hit_closest(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    const uint instanceId = InstanceID();
    const uint constantsId = meshInfos[instanceId].constantsIndex;
    const PeTriangle tri = PeHitTriangle(instanceId);
    const float3 b = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);

    PeSurfacePoint sp = PeSurfaceAtBarycentrics(tri, b);
    PeBendLimbNormal(tri, -normalize(WorldRayDirection()), sp);

    pe_P = sp.P;
    pe_N = sp.N;
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

    pe_load_material(pe_materialBytes, constants[constantsId].materialByteOffset + PE_OPENPBR_INPUTS_OFFSET);
    pe_fill_vertex_data(sp);
#if defined(GI_PROBES) && !defined(REFLECTIONS)
    const float3 color = PeSurfaceCacheHit(sp);
#else
    float3 color = pe_material_shade(sp.nextUv, sp.spriteBlend).rgb;

    // As material pass 2: scene-linear under linear_color, display-referred otherwise (as legacy hits are then).
    if (cb_linearColor == 0)
        color = LinearToSrgb(color);
#endif

    payload.radiance = any(isnan(color)) || any(isinf(color)) ? float3(0.0, 0.0, 0.0) : color;
    payload.t = RayTCurrent();
    payload.hitKind = HitKind();
    payload.alpha = 1.0;
}

// Alpha-cut MaterialX meshes: the graph's own opacity decides what rays see, with material pass coverage's rule
// (MaterialMaskMain.slang). Shadow rays through a covered but partly transparent texel keep the legacy hit's tint.
[shader("anyhit")]
void pe_hit_any(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    const uint instanceId = InstanceID();
    const uint constantsId = meshInfos[instanceId].constantsIndex;
    const PeTriangle tri = PeHitTriangle(instanceId);
    const float3 b = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    const PeSurfacePoint sp = PeSurfaceAtBarycentrics(tri, b);

    pe_P = sp.P;
    pe_N = sp.N;
    pe_load_material(pe_materialBytes, constants[constantsId].materialByteOffset + PE_OPENPBR_INPUTS_OFFSET);
    const float opacity = pe_material_mask(sp, sp.nextUv, sp.spriteBlend);
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
