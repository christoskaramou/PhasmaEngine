float PeMaskTangentSign(uint3 vertices)
{
    const PeVertex v0 = PeLoadVertex(pe_geometry, pc.verticesOffset, vertices.x);
    const PeVertex v1 = PeLoadVertex(pe_geometry, pc.verticesOffset, vertices.y);
    const PeVertex v2 = PeLoadVertex(pe_geometry, pc.verticesOffset, vertices.z);
    return PeUvWindingSign(v0.uv, v1.uv, v2.uv);
}

PeSurfacePoint PeMaskSurface(VS_OUTPUT_OpenPBRMask input, uint3 vertices)
{
    // The vertex loads are skipped unless the mesh has no tangents.
    const bool tangentless = dot(input.tangent.xyz, input.tangent.xyz) < 1e-8;
    const float fallbackSign = tangentless ? PeMaskTangentSign(vertices) : 1.0;
    return PeRasterSurface(input.position,
                           input.worldPosition,
                           input.normal,
                           input.tangent,
                           input.uv,
                           input.nextUv,
                           input.spriteBlend,
                           input.color,
                           fallbackSign);
}

#ifdef PE_MASK_SHADOW
// Shadows cast the camera's cutout, except at alpha_cutoff 0, where opacity is coverage (OpenPBR's geometry_opacity, as
// RT shadow rays already take it): a 4x4 ordered dither keeps that fraction of each block of shadow texels, and the
// lighting pass's PCF averages it into a partial shadow.
static const uint kPeShadowDither[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

float PeShadowThreshold(float alphaCut, float2 texel)
{
    if (alphaCut > 0.0)
        return alphaCut;

    const uint2 cell = uint2(texel) & 3u;
    return (float(kPeShadowDither[cell.y * 4u + cell.x]) + 0.5) / 16.0;
}

void mainPS(VS_OUTPUT_OpenPBRMask input)
#else
uint4 mainPS(VS_OUTPUT_OpenPBRMask input) : SV_TARGET0
#endif
{
    const uint e0 = GetAttributeAtVertex(input.maskEligible, 0);
    const uint e1 = GetAttributeAtVertex(input.maskEligible, 1);
    const uint e2 = GetAttributeAtVertex(input.maskEligible, 2);
    if ((e0 & e1 & e2) == 0u)
        discard;

    const uint3 vertices = uint3(GetAttributeAtVertex(input.vertexId, 0),
                                 GetAttributeAtVertex(input.vertexId, 1),
                                 GetAttributeAtVertex(input.vertexId, 2));
    const PeSurfacePoint sp = PeMaskSurface(input, vertices);
    pe_load_material(pc.shape, pe_materialBytes, constants[input.id].materialByteOffset + PE_OPENPBR_INPUTS_OFFSET);
    const float opacity = pe_material_mask(pc.shape, sp, sp.nextUv, sp.spriteBlend);
#ifdef PE_MASK_SHADOW
    const float cut = PeShadowThreshold(constants[input.id].alphaCut, input.position.xy);
#else
    const float cut = constants[input.id].alphaCut;
#endif
    if (!(opacity > 0.0 && opacity >= cut))
        discard;

#ifndef PE_MASK_SHADOW
    return uint4(input.id + 1u, vertices);
#endif
}
