// MaterialX's own transparent output: the surface colour already carries the opacity factor, and with
// TRANSMISSION_REFRACTION its alpha, 1 - avg(transparency), is the evaluated geometry_opacity (HwSurfaceNode.cpp).
float4 mainPS(VS_OUTPUT_Gbuffer input) : SV_TARGET0
{
    const float fallbackSign = PeUvWindingSign(GetAttributeAtVertex(input.uvVertex, 0),
                                               GetAttributeAtVertex(input.uvVertex, 1),
                                               GetAttributeAtVertex(input.uvVertex, 2));
    const PeSurfacePoint sp = PeRasterSurface(input.position,
                                              input.positionWS.xyz,
                                              input.normal,
                                              input.tangent,
                                              input.uv,
                                              input.nextUv,
                                              input.spriteBlend,
                                              input.color,
                                              fallbackSign);

    pe_P = sp.P;
    pe_N = sp.N;
    pe_pixel = uint2(input.position.xy);
    pe_ssao = 1.0;
    pe_occlusion = 1.0;

    pe_load_material(pe_materialBytes, pe_constants[input.id].materialByteOffset + PE_OPENPBR_INPUTS_OFFSET);
    pe_fill_vertex_data(sp);
    const PeSurfaceInputs inputs = pe_material_inputs(sp.nextUv, sp.spriteBlend);
    float3 color = pe_shade_inputs(inputs).rgb;
    const float alpha = saturate(inputs.geometry_opacity);
    if (alpha < 0.001) // MaterialX's transparent-surface threshold (HwShaderGenerator.cpp)
        discard;

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

    return float4(color, alpha);
}
