float PeMaskTangentSign(uint3 vertices)
{
    const PeVertex v0 = PeLoadVertex(pe_geometry, pc.verticesOffset, vertices.x);
    const PeVertex v1 = PeLoadVertex(pe_geometry, pc.verticesOffset, vertices.y);
    const PeVertex v2 = PeLoadVertex(pe_geometry, pc.verticesOffset, vertices.z);
    const float2 d1 = v1.uv - v0.uv;
    const float2 d2 = v2.uv - v0.uv;
    return d1.x * d2.y - d2.x * d1.y < 0.0 ? -1.0 : 1.0;
}

PeSurfacePoint PeMaskSurface(VS_OUTPUT_OpenPBRMask input, uint3 vertices)
{
    PeSurfacePoint sp;
    sp.svPosition = input.position;
    sp.P = input.worldPosition;
    sp.N = normalize(input.normal);
    sp.uv0 = input.uv;
    sp.nextUv = input.nextUv;
    sp.spriteBlend = input.spriteBlend;
    sp.color0 = input.color;

    float3 tangent = input.tangent.xyz;
    float sign = input.tangent.w;
    const float3 e1 = ddx(sp.P);
    const float3 e2 = ddy(sp.P);
    const float2 d1 = ddx(sp.uv0);
    const float2 d2 = ddy(sp.uv0);
    if (dot(tangent, tangent) < 1e-8)
    {
        // The same triangle UV tangent as PeSurfaceAt, derived in this raster's projection.
        const float det = d1.x * d2.y - d2.x * d1.y;
        tangent = (e1 * d2.y - e2 * d1.y) * (det < 0.0 ? -1.0 : 1.0);
        // Screen derivatives recover dP/du, but source UV winding owns the bitangent sign.
        sign = PeMaskTangentSign(vertices);
    }

    tangent -= sp.N * dot(sp.N, tangent);
    if (dot(tangent, tangent) < 1e-12)
    {
        tangent = abs(sp.N.x) < 0.9 ? cross(sp.N, float3(1.0, 0.0, 0.0))
                                  : cross(sp.N, float3(0.0, 1.0, 0.0));
    }

    sp.T = normalize(tangent);
    sp.B = cross(sp.N, sp.T) * (sign < 0.0 ? -1.0 : 1.0);
    return sp;
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
