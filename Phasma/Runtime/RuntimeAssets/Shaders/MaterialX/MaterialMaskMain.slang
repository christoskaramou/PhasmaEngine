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
    pe_load_material(pe_materialBytes, constants[input.id].materialByteOffset + PE_OPENPBR_INPUTS_OFFSET);
    const float opacity = pe_material_mask(sp, sp.nextUv, sp.spriteBlend);
    if (!(opacity > 0.0 && opacity >= constants[input.id].alphaCut))
        discard;

#ifndef PE_MASK_SHADOW
    return uint4(input.id + 1u, vertices);
#endif
}
