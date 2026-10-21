// One group shades one 8x8-pixel tile. Stage 1 evaluates the graph inputs quad-mapped over the tile's four 4x4-pixel
// quadrants (each pixel's 2x2 quad on its own triangle, as their derivatives need); stage 2 runs MaterialX's BSDF once
// per pixel on what stage 1 left in groupshared.
groupshared PeSurfaceInputs gs_inputs[64];
groupshared float gs_curvature[64];
groupshared uint gs_valid[64];

// The pixel's visibility sample when a MaterialX draw of this dispatch's module covers it.
bool PeShadedPixel(uint2 pixel, uint2 size, out uint4 vis)
{
    vis = uint4(0u, 0u, 0u, 0u);
    if (any(pixel >= size))
        return false;

    vis = pe_visibility[pixel];
    return vis.x != 0u && PeMatchesModule(pe_materialBytes, pe_constants[vis.x - 1u], pc.module_index);
}

// The surface point at `at`'s centre on the pixel's own triangle, with the per-pixel state the generated code reads.
PeSurfacePoint PeBeginPixel(uint4 vis, uint2 pixel, uint2 at, uint2 size)
{
    const PeTriangle tri = PeLoadTriangle(pe_geometry, pe_frameData, pe_deformed,
                                         pe_constants, pc.vertices_offset, vis);
    PeSurfacePoint sp = PeSurfaceAt(tri, float2(at) + 0.5, float2(size));
    PeBendLimbNormal(tri, normalize(cb_camPos.xyz - sp.P), sp);

    pe_P = sp.P;
    pe_N = sp.N;
    pe_pixel = pixel;
    pe_ssao = cb_ssao != 0 ? Ssao.SampleLevel(sampler_Ssao, (float2(pixel) + 0.5) / float2(size), 0).x : 1.0;
    pe_occlusion = MetRough.Load(int3(pixel, 0)).x;

    pe_load_material(pe_materialBytes, pe_constants[vis.x - 1u].materialByteOffset + PE_OPENPBR_INPUTS_OFFSET);
    pe_fill_vertex_data(sp);
    return sp;
}

// Stage 1: a quadrant's 16 pixels are the group's 2x2 thread quads; the lane at the pixel itself keeps the results.
void PeEvaluateTile(uint2 tile, uint2 local, uint2 size)
{
    for (uint quadrant = 0u; quadrant < 4u; ++quadrant)
    {
        const uint2 tid = tile * 16u + uint2(quadrant & 1u, quadrant >> 1u) * 8u + local.xy;
        const uint2 pixel = tid >> 1;
        const uint2 lane = tid & 1u;
        const uint slot = (pixel.y & 7u) * 8u + (pixel.x & 7u);
        const bool writer = all(lane == (pixel & 1u));

        uint4 vis;
        const bool shaded = PeShadedPixel(pixel, size, vis); // quad-uniform
        if (shaded)
        {
            const PeSurfacePoint sp = PeBeginPixel(vis, pixel, (pixel & ~1u) + lane, size);
            const PeSurfaceInputs p = pe_material_inputs(sp.nextUv, sp.spriteBlend);

            // MaterialX's subsurface curvature (pe_curvature), on the forward-facing normal mx_subsurface_bsdf passes it.
            const float3 V = normalize(pe_view_position() - sp.P);
            const float3 N = dot(p.geometry_normal, V) < 0.0 ? -p.geometry_normal : p.geometry_normal;
            const float curvature = length(fwidth(N)) / length(fwidth(sp.P));

            if (writer)
            {
                gs_inputs[slot] = p;
                gs_curvature[slot] = curvature;
            }
        }

        if (writer)
            gs_valid[slot] = shaded ? 1u : 0u;
    }
}

[numthreads(8, 8, 1)]
[DerivativeGroupQuad]
void mainCS(uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID, uint index : SV_GroupIndex)
{
    uint2 tile;
    if (!PeBinTile(pe_bins, pc.bins_count, pc.bins_tiles, group.y * PE_BIN_ROW_TILES + group.x, tile))
        return;

    if (pc.shadow_debug_mode != 0u) // shadow debug views stay LightingPS's
        return;

    uint2 size;
    pe_visibility.GetDimensions(size.x, size.y);

    PeEvaluateTile(tile, local.xy, size);
    GroupMemoryBarrierWithGroupSync();

    // Stage 2: thread i shades pixel i of the tile.
    if (gs_valid[index] == 0u)
        return;

    const uint2 pixel = tile * 8u + local.xy;
    const PeSurfacePoint sp = PeBeginPixel(pe_visibility[pixel], pixel, pixel, size);
    pe_shade_curvature = gs_curvature[index];
    float3 color = pe_shade_inputs(gs_inputs[index]).rgb;

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

    if (!any(isnan(color)) && !any(isinf(color)))
        pe_viewportOut[pixel] = float4(color, 1.0);
}
