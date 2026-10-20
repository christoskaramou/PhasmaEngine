#include "RTCommon.hlsl"

[shader("raygeneration")]
void raygeneration()
{
#ifdef SCREEN_PROBES
    // Screen probes (ScreenProbePlace.hlsl): 8x8 rays per tile probe. A ray that runs out of length takes the world
    // probes' far-field radiance; it runs to the far-field start plus the distance to the farthest world probe it
    // interpolates (about the same again), so no surface falls between where it ends and where a world probe's
    // far-field ray begins. A back-face hit means the ray slipped into a closed interior and carries no light.
    uint2 launch = DispatchRaysIndex().xy;
    uint2 tile = launch / 8;
    float4 probe = sp_probePositions.Load(int3(tile, 0));
    float3 probeNormal = sp_probeNormals.Load(int3(tile, 0)).xyz;
    float4 directionPdf = gi_rayDirections.Load(int3(launch, 0));
    if (probe.w <= 0.0 || directionPdf.w <= 0.0 || dot(probeNormal, directionPdf.xyz) <= 0.0)
    {
        output[launch] = 0.0.xxxx; // no probe, or a direction under the surface (dark lower hemisphere)
        return;
    }
    RayDesc ray;
    ray.Origin = probe.xyz + probeNormal * max(0.01, 0.002 * probe.w);
    ray.Direction = directionPdf.xyz;
    ray.TMin = 0.001;
    float handOffEdge;
    uint handOff = min(ProbeCascadeAt(probe.xyz, false, handOffEdge), max(gi_cascadeCount, 1u) - 1);
    ray.TMax = 2.0 * ProbeFarFieldStart(handOff);
    HitPayload payload = (HitPayload)0;
    payload.t = -1.0;
    payload.alpha = 1.0;
    payload.opaqueTMax = ray.TMax;
    TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
    float3 radiance;
    if (payload.t >= 0.0)
        radiance = payload.hitKind == HIT_KIND_TRIANGLE_BACK_FACE ? 0.0.xxx : payload.radiance;
    else
        radiance = WorldRadianceCache(probe.xyz, probeNormal, ray.Direction, payload.radiance);
    output[launch] = float4(max(radiance, 0.0), payload.t >= 0.0 ? payload.t : ray.TMax);
#elif defined(REFLECTIONS)
    // Reflections (ReflectionCommon.hlsl): one GGX ray per pixel up to REFLECTION_MAX_ROUGHNESS, long
    // enough to reach anything in the scene. Hits are shaded like probe hits (direct light, world probe irradiance,
    // emission); misses take the sky. rgb radiance, a hit distance (< 0: nothing traced).
    uint2 launch = DispatchRaysIndex().xy;
    uint2 pixel = launch;
    uint width, height;
    lr_traceDepth.GetDimensions(width, height);
    float depth = all(pixel < uint2(width, height)) ? lr_traceDepth.Load(int3(pixel, 0)) : 0.0;
    float roughness = lr_traceMaterial.Load(int3(pixel, 0)).y;
    if (depth == 0.0 || roughness >= REFLECTION_MAX_ROUGHNESS)
    {
        output[launch] = float4(0.0, 0.0, 0.0, -1.0);
        return;
    }
    float3 position = GetPosFromUV((float2(pixel) + 0.5) / float2(width, height), depth, cb_invViewProj);
    float3 N = normalize(lr_traceNormal.Load(int3(pixel, 0)).xyz * 2.0 - 1.0);
    float3 V = normalize(cb_camPos.xyz - position);
    float4 sample = ReflectionSample(N, V, roughness, ReflectionRandom(pixel, gi_frame));
    if (dot(sample.xyz, N) <= 0.0)
    {
        output[launch] = 0.0.xxxx; // under the surface: a sample without light
        return;
    }
    RayDesc ray;
    ray.Origin = position + N * max(0.01, 0.002 * length(cb_camPos.xyz - position));
    ray.Direction = sample.xyz;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    HitPayload payload = (HitPayload)0;
    payload.t = -1.0;
    payload.alpha = 1.0;
    payload.opaqueTMax = ray.TMax;
    TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
    float3 radiance = payload.t >= 0.0 && payload.hitKind == HIT_KIND_TRIANGLE_BACK_FACE ? 0.0.xxx : payload.radiance;
    output[launch] = float4(max(radiance, 0.0), payload.t >= 0.0 ? payload.t : ray.TMax);
#elif defined(GI_PROBES)
    uint2 launch = DispatchRaysIndex().xy;
    uint cascade = ProbeCascade(launch.y);
    RayDesc ray;
    ray.Origin = ProbePosition(launch.y);
    ray.Direction = gi_rayDirections.Load(int3(launch.xy, 0)).xyz;
    ray.TMin = 0.001;
    ray.TMax = ProbeMaxDistance(cascade);
    HitPayload payload = (HitPayload)0;
    payload.t = -1.0;
    payload.alpha = 1.0;
    payload.opaqueTMax = ray.TMax;
    TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
    float distance = payload.t < 0.0 ? ray.TMax : payload.t;
    if (payload.hitKind == HIT_KIND_TRIANGLE_BACK_FACE && payload.t >= 0.0)
        distance = -distance;
    output[launch] = float4(max(payload.radiance, 0.0), distance);
    // Far-field radiance for the screen probes' hand-off (Lumen's radiance cache): what the ray sees beyond the probe's
    // interpolation footprint, so a screen probe interpolating this probe never takes light from the far side of a
    // thin wall next to it. A ray that already reaches past the footprint keeps its own hit; one continued from inside
    // geometry (a back face first) has none.
    float footprint = ProbeFarFieldStart(cascade);
    float4 far = float4(max(payload.radiance, 0.0), distance < 0.0 ? -1.0 : 1.0);
    if (payload.t >= 0.0 && payload.t < footprint && footprint < ray.TMax)
    {
        RayDesc farRay = ray;
        farRay.Origin = ray.Origin + ray.Direction * footprint;
        farRay.TMax = ray.TMax - footprint;
        HitPayload farPayload = (HitPayload)0;
        farPayload.t = -1.0;
        farPayload.alpha = 1.0;
        farPayload.opaqueTMax = farRay.TMax;
        TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 0, 0, farRay, farPayload);
        bool inside = farPayload.t >= 0.0 && farPayload.hitKind == HIT_KIND_TRIANGLE_BACK_FACE;
        far = float4(max(farPayload.radiance, 0.0), inside ? -1.0 : 1.0);
    }
    gi_farRays[launch] = far;
#else
    uint3 launchIndex = DispatchRaysIndex();
    uint3 launchDim   = DispatchRaysDimensions();

    // Pixel center in NDC (-1..1)
    float2 pixelCenter = (float2)launchIndex.xy + 0.5.xx;
    float2 uv          = pixelCenter / (float2)launchDim.xy;
    float2 ndc         = uv * 2.0 - 1.0;

    float4x4 invView = LoadMatrix(128); // Offset 128 is invView (64*2)
    float4x4 invProjection = LoadMatrix(192); // Offset 192 is invProjection (64*3)

    // Unproject an NDC point to view space.
    float4 pView = mul(float4(ndc.x, ndc.y, 1.0, 1.0), invProjection);
    pView.xyz /= pView.w;

    float3 originView = 0.0.xxx;
    float3 dirView = normalize(pView.xyz);
    if (cb_orthographicCamera != 0)
    {
        originView = float3(pView.x, pView.y, 0.0);
        dirView = float3(0.0, 0.0, 1.0);
    }

    // Transform ray to world space.
    float3 originWorld = mul(float4(originView, 1.0), invView).xyz;
    float3 dirWorld = normalize(mul(dirView, (float3x3)invView));

    // MODE-BASED CONFIGURATION
    // 0 = Raster
    // 1 = Hybrid
    // 2 = RayTracing
    uint mask = 0xFF;       // Trace all geometry
    float tMax = 100000.0;  // Unlimited distance

    // Hybrid only: RayTracingPass declares the depth read only then, since full RT may run no depth pass.
    if (cb_renderMode != 2)
    {
        // Read depth from raster
        float opaqueDepth = depthBuffer.Load(int3(launchIndex.xy, 0)).r;

        // Calculate distance to surface
        float4 pViewOpaque = mul(float4(ndc.x, ndc.y, opaqueDepth, 1.0), invProjection);
        pViewOpaque.xyz /= pViewOpaque.w;
        float opaqueDist = length(pViewOpaque.xyz - originView);

        mask = 0x80; // Transparent only
        // Clamped by raster depth — which includes transparent surfaces (the "transparent" pass variant
        // writes depth), so at a glass pixel the nearest depth IS the glass. Overshoot slightly so the
        // ray reliably reaches it; the transparent-only mask keeps opaque geometry unexposed.
        tMax = opaqueDist * 1.001 + 0.001;
    }

    RayDesc ray;
    ray.Origin    = originWorld;
    ray.Direction = dirWorld;
    ray.TMin      = 0.001;
    ray.TMax      = tMax;

    // Payload init
    HitPayload payload;
    payload.radiance = 0.0.xxx;
    payload.t        = -1.0;
    payload.hitKind  = 0;
    payload.rayType  = 0;
    payload.depth    = 0;
    payload.isRefractive = 0;
    payload.alpha    = 1.0;
    payload.opaqueTMax = tMax;

    TraceRay(
        tlas,
        RAY_FLAG_NONE,
        mask,
        0,  // hit group index base (ray type 0)
        0,  // geometry contribution multiplier
        0,  // miss shader index
        ray,
        payload
    );

    if (payload.t != -1.0)
    {
        output[launchIndex.xy] = float4(payload.radiance, payload.alpha);
    }
    else if (cb_renderMode == 2) // With full RT, show skybox when no hit
    {
        float3 direction = normalize(dirWorld);
        output[launchIndex.xy] = float4(skybox.SampleLevel(material_sampler, direction, 0).rgb, 1.0);
    }

    // Full RT: store the primary hit's device depth for RTDepthResolvePass, which stamps it into
    // the depth buffer so depth-testing overlays (grid, lines, selection outline) keep working.
    if (cb_renderMode == 2)
    {
        float deviceZ = 0.0; // reverse-Z far
        if (payload.t > 0.0)
        {
            float3 hitWorld = originWorld + dirWorld * payload.t;
            float4 clip = mul(float4(hitWorld, 1.0), GetViewProjection());
            deviceZ = clip.w > 0.0 ? saturate(clip.z / clip.w) : 0.0;
        }
        rtDepth[launchIndex.xy] = deviceZ;
    }
#endif
}

[shader("anyhit")]
void anyhit(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
#ifdef GI_PROBES
    uint probeRenderType = meshInfos[InstanceID()].renderType;
    if (payload.rayType == 0 && (probeRenderType == 3 || probeRenderType == 4))
    {
        IgnoreHit();
        return;
    }
#endif
    uint instanceId = InstanceID();
    uint constantsId = meshInfos[instanceId].constantsIndex;
    uint primitiveId = PrimitiveIndex();

    uint3 indices = GetIndices(instanceId, primitiveId);
    Vertex v0 = SkinVertex(GetVertex(instanceId, indices.x), constantsId);
    Vertex v1 = SkinVertex(GetVertex(instanceId, indices.y), constantsId);
    Vertex v2 = SkinVertex(GetVertex(instanceId, indices.z), constantsId);

    float3 barycentricCoords = float3(1.0f - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    float2 uv = v0.uv * barycentricCoords.x + v1.uv * barycentricCoords.y + v2.uv * barycentricCoords.z;
    float4 color = v0.color * barycentricCoords.x + v1.color * barycentricCoords.y + v2.color * barycentricCoords.z;

    uint textureMask = constants[constantsId].textureMask;
    MaterialGpuData mat = LoadMaterial(constantsId);
    float4 baseColor = color * mat.baseColorFactor;
    if (HasTexture(textureMask, TEX_BASE_COLOR_BIT))
        baseColor *= GetBaseColor(constantsId, uv);
    // Alpha-cut discard applies to AlphaCut materials only: Transmission and AlphaBlend author
    // baseColor.a < 1 for the raster path (their transparency comes from refraction / ray
    // continuation), so discarding them here would make those surfaces invisible to RT rays.
    uint renderType = meshInfos[instanceId].renderType;
    if (renderType == 2 && baseColor.a < constants[constantsId].alphaCut)
    {
        IgnoreHit();
    }
    else if (payload.rayType == 1 && baseColor.a < 1.0)
    {
        payload.radiance *= (1.0 - baseColor.a);
        IgnoreHit();
    }
}

[shader("closesthit")]
void closesthit(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    uint instanceId = InstanceID();
    uint constantsId = meshInfos[instanceId].constantsIndex;
    uint primitiveId = PrimitiveIndex();
    uint3 indices = GetIndices(instanceId, primitiveId);

    // Interpolate vertex attributes (apply skinning if weighted)
    Vertex v0 = SkinVertex(GetVertex(instanceId, indices.x), constantsId);
    Vertex v1 = SkinVertex(GetVertex(instanceId, indices.y), constantsId);
    Vertex v2 = SkinVertex(GetVertex(instanceId, indices.z), constantsId);

    float3 barycentricCoords = float3(1.0f - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);

    float2 uv = v0.uv * barycentricCoords.x + v1.uv * barycentricCoords.y + v2.uv * barycentricCoords.z;
    float3 normalObj = v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z;
    float4 tangentObj = v0.tangent * barycentricCoords.x + v1.tangent * barycentricCoords.y + v2.tangent * barycentricCoords.z;
    float4 color = v0.color * barycentricCoords.x + v1.color * barycentricCoords.y + v2.color * barycentricCoords.z;

    // World Space conversion
    float3x4 objToWorld = ObjectToWorld3x4();
    float3x3 worldRotation3x3 = remove_scale3x3((float3x3)objToWorld);
    float3 normalWorld = normalize(mul(normalObj, worldRotation3x3));
    float3 positionWorld = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

    // Tangent Space
    float3 tangentWorld = normalize(mul(tangentObj.xyz, worldRotation3x3));

    // GBuffer Logic Adaptation
    uint textureMask = constants[constantsId].textureMask;
    MaterialGpuData mat = LoadMaterial(constantsId);
    uint renderType = meshInfos[instanceId].renderType;
    bool isAlphaCutMaterial = renderType == 2;
    bool isAlphaBlendMaterial = renderType == 3;
    bool isTransmissive = renderType == 4;

    // 1. Base Color
    float4 combinedColor = color * mat.baseColorFactor;
    if (cb_linearColor != 0) // vertex colour and factor are each sRGB-authored
        combinedColor.rgb = SrgbToLinear(color.rgb) * SrgbToLinear(mat.baseColorFactor.rgb);
    if (HasTexture(textureMask, TEX_BASE_COLOR_BIT))
    {
        combinedColor *= GetBaseColor(constantsId, uv);
    }
    combinedColor = saturate(combinedColor);

    // 2. Material Properties
    float4 metRoughAlphacutOcl = mat.pbrParams;
    float metallic = metRoughAlphacutOcl.x;
    float roughness = metRoughAlphacutOcl.y;
    if (HasTexture(textureMask, TEX_METAL_ROUGH_BIT))
    {
        float3 mrSample = GetMetallicRoughness(constantsId, uv).xyz;
        metallic *= mrSample.z;
        roughness *= mrSample.y;
    }
    metallic = saturate(metallic);
    roughness = saturate(roughness);

    float occlusion = 1.0f;
    if (HasTexture(textureMask, TEX_OCCLUSION_BIT))
    {
        float occlusionSample = GetOcclusion(constantsId, uv).r;
        occlusion = lerp(1.0f, occlusionSample, metRoughAlphacutOcl.w);
    }

    float3 emissive = cb_linearColor != 0 ? SrgbToLinearHdr(mat.emissiveTransmission.xyz) : mat.emissiveTransmission.xyz;
    if (HasTexture(textureMask, TEX_EMISSIVE_BIT))
    {
        emissive *= GetEmissive(constantsId, uv).xyz;
    }
    emissive = max(emissive, 0.0);
    // Self-lit is an authoring signal for alpha cards (emissive mirrors albedo to mean "unlit").
    // Genuinely emissive opaque materials must take the standard lit path below, which adds the
    // emissive term; albedo * SelfLitMaterialScale renders dim-albedo/strong-emissive near-black.
    bool rtOwnsMaterial = isAlphaCutMaterial || isAlphaBlendMaterial;
    bool selfLitMaterial = rtOwnsMaterial && IsSelfLitMaterial(emissive, mat);

    // 3. Normal Mapping
    float3 N = normalWorld;
    if (HasTexture(textureMask, TEX_NORMAL_BIT))
    {
        float3 tangentNormalSample = GetNormal(constantsId, uv).xyz;
        tangentWorld = normalize(tangentWorld - dot(tangentWorld, normalWorld) * normalWorld);
        float3 bitangentWorld = cross(normalWorld, tangentWorld) * tangentObj.w;
        float3x3 TBN = float3x3(tangentWorld, bitangentWorld, normalWorld);
        N = normalize(mul(tangentNormalSample * 2.0 - 1.0, TBN));
    }

    // 4. Lighting (IBL)
    float3 V = normalize(-WorldRayDirection());
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, combinedColor.rgb, metallic);
#ifdef GI_PROBES
    // GI and reflection rays light their hits as Lumen's surface cache does: diffuse only, the specular colour folded
    // into the albedo as Unreal's EnvBRDFApproxFullyRough (diffuse + 0.45 x specular colour). A sharp highlight on a
    // mirror would otherwise reach the probes as fireflies.
    if (!selfLitMaterial)
    {
        combinedColor.rgb = combinedColor.rgb * (1.0 - metallic) + F0 * 0.45;
        metallic = 0.0;
        roughness = 1.0;
        F0 = 0.0.xxx;
    }
#endif

    // Energy Compensation
    float NdV = clamp(dot(N, V), 0.001, 1.0);
    float2 envBRDF = LutIBL.SampleLevel(material_sampler, float2(NdV, roughness), 0).xy;
    float E = envBRDF.x + envBRDF.y;
    float3 energyCompensation = 1.0 + F0 * (1.0 / max(E, 0.001) - 1.0);

    float3 lighting = 0.0.xxx;

    // Self-lit alpha cards render as unlit albedo.
    if (selfLitMaterial)
    {
        lighting = combinedColor.rgb;
        if (cb_renderMode == 2 && !isAlphaCutMaterial)
            lighting *= SelfLitMaterialScale(emissive, mat);
        if (cb_renderMode == 2 && isAlphaBlendMaterial)
            lighting *= combinedColor.a;
    }
    else
    {
        float diffuseIBL = 1.0;
#if defined(GI_PROBES) || defined(GI_SHADING)
        float4 indirect = ProbeLighting(positionWorld, N);
        float indirectScale = 1.0;
#ifdef GI_SHADING
        indirectScale = gi_intensity;
#endif
        if (indirect.a > 0.0 && indirectScale > 0.0)
        {
            float3 diffuseWeight = max(0.0, 1.0 - (F0 * envBRDF.x + envBRDF.y) * energyCompensation) * (1.0 - metallic);
            lighting += indirect.rgb * combinedColor.rgb * diffuseWeight * (indirectScale * indirect.a / PI);
            diffuseIBL = 1.0 - indirect.a;
        }
#endif
        if (cb_IBL != 0)
            lighting += ComputeIBL(N, V, combinedColor.rgb, metallic, roughness, F0, envBRDF, diffuseIBL) * cb_iblIntensity;
        lighting *= occlusion;

        // Direct Lighting
        // Assuming single sun for now, or loop for multiple
        DirectionalLight sun = LoadDirectionalLight(0);
        float3 sunDir = normalize(RotateVectorByQuat(float3(0, 0, -1), sun.rotation));
        float3 L = normalize(-sunDir); // Negate: direction is ray dir, L is to-light
        float sunShadow = TraceShadowRay(positionWorld, L, 10000.0);
        for (uint i = 0; i < cb_numDirectionalLights; i++)
        {
            // Only shadowing the first one for now as per previous logic (or optimize shadow tracing)
            float s = (i == 0) ? sunShadow : 1.0;
            lighting += RT_DirectLight(LoadDirectionalLight(i), positionWorld, N, V, combinedColor.rgb, metallic, roughness, F0, occlusion, s, energyCompensation);
        }

        // Point Lights
        for (uint p = 0; p < cb_numPointLights; p++)
        {
            if (LoadPointLight(p).color.w > 0.0) // intensity is .w
            {
                float dist = distance(positionWorld, LoadPointLight(p).position.xyz); // position is .xyz
                if (dist < LoadPointLight(p).position.w) // radius is .w
                    lighting += RT_ComputePointLight(p, positionWorld, N, V, combinedColor.rgb, metallic, roughness, F0, occlusion, energyCompensation);
            }
        }

        // Spot Lights
        for (uint j = 0; j < cb_numSpotLights; j++)
        {
            if (LoadSpotLight(j).color.w > 0.0) // intensity is .w
                lighting += RT_ComputeSpotLight(j, positionWorld, N, V, combinedColor.rgb, metallic, roughness, F0, occlusion, energyCompensation);
        }

        // Area Lights
        for (uint k = 0; k < cb_numAreaLights; k++)
        {
            if (LoadAreaLight(k).color.w > 0.0)
                lighting += RT_ComputeAreaLight(k, positionWorld, N, V, combinedColor.rgb, metallic, roughness, F0, occlusion, energyCompensation);
        }

        if (!isTransmissive)
            lighting += emissive;
    }

    // Transmission & Volume
    float transmissionFactor = mat.emissiveTransmission.w;
    float thicknessFactor = mat.transmissionVolume.x;
    float attenuationDistance = mat.transmissionVolume.y;
    float ior = mat.transmissionVolume.z;
    float3 attenuationColor = mat.attenuationColor.xyz;

    // Check material type from RenderType enum (1: Opaque, 2: AlphaCut, 3: AlphaBlend, 4: Transmission)
    bool shouldTraceSecondary = isTransmissive || (isAlphaBlendMaterial && combinedColor.a < 1.0f);
    if (shouldTraceSecondary && payload.depth < 2) // 2 Bounces
    {
        RayDesc ray;
        ray.Origin = positionWorld;
        ray.TMin   = 0.01;
        ray.TMax   = 10000.0;

        float3 nextDir = WorldRayDirection();

        if (isTransmissive)
        {
            float r = 1.0 / ior;
            if (dot(WorldRayDirection(), N) > 0.0)
            {
                r = ior / 1.0;
                N = -N;
            }

            float3 refracted = refract(WorldRayDirection(), N, r);
            if (length(refracted) > 0.0)
                nextDir = normalize(refracted);
            else
                nextDir = reflect(WorldRayDirection(), N);
        }

        ray.Direction = isTransmissive ? nextDir : WorldRayDirection(); // refract or pass through

        bool clampToHybridOpaqueDepth = isAlphaBlendMaterial && cb_renderMode == 1;
        if (clampToHybridOpaqueDepth)
            ray.TMax = max(0.0, payload.opaqueTMax - RayTCurrent() - 0.001);

        HitPayload transPayload;
        transPayload.radiance = 0.0.xxx;
        transPayload.t        = -1.0;
        transPayload.hitKind  = 0;
        transPayload.rayType  = 0;
        transPayload.depth    = payload.depth + 1;
        transPayload.isRefractive = isTransmissive ? 1 : 0;
        transPayload.opaqueTMax = clampToHybridOpaqueDepth ? ray.TMax : 10000.0;

        // In Full RT mode, alpha blended objects need to see opaque geometry behind them
        uint mask = (isTransmissive || cb_renderMode == 2) ? 0xFF : 0x80;

        if (ray.TMax > ray.TMin)
        {
            TraceRay(
                tlas,
                RAY_FLAG_NONE,
                mask,
                0, 0, 0,
                ray,
                transPayload
            );
        }
        else if (clampToHybridOpaqueDepth)
        {
            uint3 launchIndex = DispatchRaysIndex();
            transPayload.radiance = output[launchIndex.xy].rgb;
        }

        float3 transColor = transPayload.radiance;

        // Base-color tint + Beer's-law absorption apply once per medium traversal, at the ENTRY
        // (front-face) hit, where transPayload.t is the interior chord length; exit hits pass the
        // escaped radiance through untouched (their t measures distance outside the medium).
        bool exitingSurface = isTransmissive && dot(WorldRayDirection(), normalWorld) > 0.0;
        if (isTransmissive && !exitingSurface)
        {
             transColor *= combinedColor.rgb;
             float dist = transPayload.t;

             if (thicknessFactor > 0.0)
                 dist = thicknessFactor;

             if (dist > 0.0 && attenuationDistance < 10000.0)
             {
                 float3 density = -log(attenuationColor) / attenuationDistance;
                 float3 transmission = exp(-density * dist);
                 transColor *= transmission;
             }
        }

        if (isTransmissive)
        {
            // Front faces: Fresnel-weighted mix keeps the surface reflection. Interior exit hits:
            // pure transmission — NdV clamps to 0.001 there, driving Fresnel to ~1 and returning
            // the backface's near-black surface lighting; TIR is handled by the refract branch above.
            float3 glass = exitingSurface ? transColor : lerp(transColor, lighting, Fresnel(F0, NdV));
            lighting = lerp(lighting, glass, transmissionFactor);
        }
        else
             lighting = lerp(transColor, lighting, combinedColor.a);
    }
    else if (isTransmissive)
    {
        // Bounce budget exhausted inside the glass (TIR chains): sample the environment along the
        // current direction instead of this interior face's near-black surface lighting. Untinted —
        // the entry hit applies the medium tint. Do NOT raise the bounce cap instead: depth-2 hits
        // already put their shadow rays at maxRecursionDepth (4).
        lighting = skybox.SampleLevel(material_sampler, WorldRayDirection(), 0).rgb * cb_iblIntensity;
    }

    // Glass emits on top of what it transmits, once at its entry face, as LightingPS adds emissive after its
    // transmission lerp. Mixed into the surface term above, a transmission factor of 1 cut the glow to the Fresnel weight.
    if (isTransmissive && dot(WorldRayDirection(), normalWorld) <= 0.0)
        lighting += emissive;

    payload.radiance = lighting;
    payload.t        = RayTCurrent();
    payload.hitKind  = HitKind();
    payload.alpha    = 1.0; // All hits are opaque, transparency is handled from ray continuation
}

[shader("miss")]
void miss(inout HitPayload payload)
{
    // Shadow ray check
    if (payload.rayType == 1)
    {
        payload.t = -1.0; // Use t to signal miss
        return;
    }

    if (payload.depth > 0 && payload.isRefractive == 0)
    {
        if (cb_renderMode == 1)
        {
            uint3 launchIndex = DispatchRaysIndex();
            payload.radiance = output[launchIndex.xy].rgb;
            payload.t = -1.0;
            return;
        }
    }

    float3 direction = WorldRayDirection();
    payload.radiance = skybox.SampleLevel(material_sampler, direction, 0).rgb * cb_iblIntensity;
    payload.t = -1.0;
    payload.alpha = 0.0;
}
