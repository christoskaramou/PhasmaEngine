[numthreads(8, 8, 1)]
[DerivativeGroupQuad]
void mainCS(uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID)
{
    uint2 tid;
    if (!PeBinThread(pe_bins, pc.binsCount, pc.binsTiles, group, local, tid))
        return;

    const uint2 pixel = tid.xy >> 1;
    const uint2 lane = tid.xy & 1u;
    const uint2 lanePixel = (pixel & ~1u) + lane;

    uint2 size;
    pe_visibility.GetDimensions(size.x, size.y);
    if (any(pixel >= size))
        return;

    const uint4 vis = pe_visibility[pixel];
    if (vis.x == 0u)
        return;

    if (!PeMatchesModule(pe_materialBytes, pe_constants[vis.x - 1u], pc.moduleIndex))
        return;

    const PeTriangle tri = PeLoadTriangle(pe_geometry, pe_frameData, pe_deformed, pe_constants, pc.verticesOffset, vis);
    const PeSurfacePoint sp = PeSurfaceAt(tri, float2(lanePixel) + 0.5, float2(size));

    pe_load_material(pe_materialBytes, pe_constants[vis.x - 1u].materialByteOffset + PE_OPENPBR_INPUTS_OFFSET);
    const PeThin thin = pe_material_thin(sp, sp.nextUv, sp.spriteBlend);

    if (!all(lane == (pixel & 1u)))
        return;

    // Never overwrite the raster G-buffer with a non-finite result: keep its value for this texel.
    if (any(isnan(thin.normal)) || any(isinf(thin.normal)) ||
        any(isnan(thin.albedo)) || any(isinf(thin.albedo)) ||
        isnan(thin.roughness) || isinf(thin.roughness) ||
        isnan(thin.metalness) || isinf(thin.metalness))
        return;

    // GBufferPS's encodings: normal * 0.5 + 0.5; albedo as the colour authored for the display (sRGB-encoded in both
    // colour modes: linear_color decodes it in lighting, the legacy pipeline lights it as is); srm = (AO, roughness,
    // metalness, transmission flag); emissive alpha 1 (not night-emissive).
    pe_normalOut[pixel] = float4(thin.normal * 0.5 + 0.5, 1.0);

    const float3 albedo = saturate(thin.albedo);
    pe_albedoOut[pixel] = float4(LinearToSrgb(albedo), 1.0);

    // The raster pass samples glTF occlusion and applies its strength; AO stays outside the MaterialX graph.
    const float occlusion = pe_srmOut[pixel].x;
    pe_srmOut[pixel] = float4(occlusion, saturate(thin.roughness), saturate(thin.metalness), 0.0);
    pe_emissiveOut[pixel] = float4(0.0, 0.0, 0.0, 1.0);
}
