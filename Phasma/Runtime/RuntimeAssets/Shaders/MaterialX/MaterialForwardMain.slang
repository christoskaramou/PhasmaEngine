struct PeForwardOutput
{
    float4 color : SV_TARGET0;
#ifdef PE_FORWARD_VELOCITY
    float4 velocity : SV_TARGET1; // xy as GBufferPS; w is the blend's source alpha
#endif
#if defined(PE_FORWARD_DOF_DEPTH) && defined(PE_FORWARD_VELOCITY)
    float dofDepth : SV_TARGET2; // max-blended: the nearest surface that hides what is behind it, 0 for none
#elif defined(PE_FORWARD_DOF_DEPTH)
    float dofDepth : SV_TARGET1;
#endif
};

// MaterialX's own transparent output: the surface colour already carries the opacity factor, and with
// TRANSMISSION_REFRACTION its alpha, 1 - avg(transparency), is the evaluated geometry_opacity (HwSurfaceNode.cpp).
PeForwardOutput mainPS(VS_OUTPUT_Gbuffer input)
{
    const float fallbackSign = PeUvWindingSign(GetAttributeAtVertex(input.uvVertex, 0),
                                               GetAttributeAtVertex(input.uvVertex, 1),
                                               GetAttributeAtVertex(input.uvVertex, 2));
    PeSurfacePoint sp = PeRasterSurface(input.position,
                                        input.positionWS.xyz,
                                        input.normal,
                                        input.tangent,
                                        input.uv,
                                        input.nextUv,
                                        input.spriteBlend,
                                        input.color,
                                        fallbackSign);

    // As material pass 2, with the facet from the position's screen derivatives (taken in uniform control flow).
    const float3 facetNormal = cross(ddx(sp.P), ddy(sp.P));
    PeBendLimbNormalFacet(facetNormal, normalize(cb_camPos.xyz - sp.P), sp);

    pe_P = sp.P;
    pe_N = sp.N;
    pe_pixel = uint2(input.position.xy);
    pe_ssao = 1.0;
    pe_occlusion = 1.0;

    const uint offset = pe_constants[input.id].materialByteOffset + PE_OPENPBR_INPUTS_OFFSET;
    const PeSurfaceInputs inputs = pe_material_inputs(pc.shape, pe_materialBytes, offset, sp, sp.nextUv, sp.spriteBlend);
    const float alpha = saturate(inputs.geometry_opacity);
    if (alpha < 0.001) // MaterialX's transparent-surface threshold (HwShaderGenerator.cpp)
        discard;

    // The depth draw blends nothing in (zero source alpha, max with 0); the shading draw keeps the nearest surface.
    PeForwardOutput output;
    output.color = float4(0.0, 0.0, 0.0, 0.0);
#ifdef PE_FORWARD_VELOCITY
    output.velocity = float4(0.0, 0.0, 0.0, 0.0);
#endif
#ifdef PE_FORWARD_DOF_DEPTH
    output.dofDepth = 0.0;
#endif
    if (pc.depth_only != 0u)
        return output;

    float3 color = pe_shade_inputs(inputs).rgb;

    if (cb_fogDensity > 0.0f) // pass 2's distance haze on the opacity-weighted colour
    {
        const float3 V = normalize(cb_camPos.xyz - sp.P);
        const float fogAmount = 1.0f - exp(-max(length(sp.P - cb_camPos.xyz) - cb_fogStart, 0.0f) * cb_fogDensity);
        const float3 fogColor = pe_fogCube.SampleLevel(sampler_pe_fogCube, -V, 2.0f).xyz;
        color = lerp(color, fogColor * alpha, fogAmount);
    }

    // MaterialX's hwSrgbEncodeOutput order: encode the colour as generated, then blend SRC_ALPHA (Decision 2 = A).
    if (cb_linearColor == 0)
        color = LinearToSrgb(color);

    if (any(isnan(color)) || any(isinf(color)))
        color = float3(0.0, 0.0, 0.0);

    output.color = float4(color, alpha);

#ifdef PE_FORWARD_VELOCITY
    // TAA reprojects with one velocity per pixel: the surface's own where it is at least half opaque, else what is
    // behind stays (forward-gaps plan Decision 1 = A).
    const float2 currentNDC = input.positionCS.xy / input.positionCS.w - pc.projJitter;
    const float2 previousNDC = input.prevPositionCS.xy / input.prevPositionCS.w - pc.prevProjJitter;
    output.velocity = float4(previousNDC - currentNDC, 0.0, alpha >= 0.5 ? 1.0 : 0.0);
#endif

#ifdef PE_FORWARD_DOF_DEPTH
    // Depth of field blurs a surface by its own depth where it hides what is behind it: at least half opaque, and not
    // a thin-walled pane that shows the scene behind at that scene's own depth.
    const bool hides = alpha >= 0.5 && !(inputs.transmission_weight > 0.0 && inputs.geometry_thin_walled);
    output.dofDepth = hides ? input.position.z : 0.0;
#endif

    return output;
}
