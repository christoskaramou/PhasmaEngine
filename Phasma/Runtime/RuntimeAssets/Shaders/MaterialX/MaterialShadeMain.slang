[numthreads(8, 8, 1)]
[DerivativeGroupQuad]
void mainCS(uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID)
{
    uint2 tid;
    if (!PeBinThread(pe_bins, pc.bins_count, pc.bins_tiles, group, local, tid))
        return;

    const uint2 pixel = tid.xy >> 1;
    const uint2 lane = tid.xy & 1u;
    const uint2 lanePixel = (pixel & ~1u) + lane;

    uint2 size;
    pe_visibility.GetDimensions(size.x, size.y);
    if (any(pixel >= size) || pc.shadow_debug_mode != 0u) // quad-uniform; shadow debug views stay LightingPS's
        return;

    const uint4 vis = pe_visibility[pixel];
    if (vis.x == 0u)
        return;

    if (!PeMatchesModule(pe_materialBytes, pe_constants[vis.x - 1u], pc.module_index))
        return;

    const PeTriangle tri = PeLoadTriangle(pe_geometry, pe_frameData, pe_deformed,
                                         pe_constants, pc.vertices_offset, vis);
    PeSurfacePoint sp = PeSurfaceAt(tri, float2(lanePixel) + 0.5, float2(size));
    PeBendLimbNormal(tri, normalize(cb_camPos.xyz - sp.P), sp);

    pe_P = sp.P;
    pe_N = sp.N;
    pe_pixel = pixel;
    pe_ssao = cb_ssao != 0 ? Ssao.SampleLevel(sampler_Ssao, (float2(pixel) + 0.5) / float2(size), 0).x : 1.0;
    pe_occlusion = MetRough.Load(int3(pixel, 0)).x;

    pe_load_material(pe_materialBytes, pe_constants[vis.x - 1u].materialByteOffset + PE_OPENPBR_INPUTS_OFFSET);
    pe_fill_vertex_data(sp);
    float3 color = pe_material_shade(sp.nextUv, sp.spriteBlend).rgb;

    if (cb_fogDensity > 0.0f) // LightingPS's distance haze, toward the blurred sky behind the pixel
    {
        const float3 V = normalize(cb_camPos.xyz - sp.P);
        const float fogAmount = 1.0f - exp(-max(length(sp.P - cb_camPos.xyz) - cb_fogStart, 0.0f) * cb_fogDensity);
        color = lerp(color, pe_fogCube.SampleLevel(sampler_pe_fogCube, -V, 2.0f).xyz, fogAmount);
    }

    // MaterialX's colour is scene-linear. Under linear_color the frame stays linear until Tonemap encodes it; otherwise the
    // whole frame is display-referred, so encode here to look as MaterialX's own renderer shows it.
    if (cb_linearColor == 0)
        color = LinearToSrgb(color);

    if (all(lane == (pixel & 1u)) && !any(isnan(color)) && !any(isinf(color)))
        pe_viewportOut[pixel] = float4(color, 1.0);
}
