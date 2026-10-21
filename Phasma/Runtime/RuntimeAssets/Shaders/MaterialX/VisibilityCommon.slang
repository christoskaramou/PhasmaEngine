// Triangle reconstruction for the visibility-buffer resolve (debug view, material pass 1 and 2).
// Binding-free: each pass declares its resources and passes them in, so one file serves every set layout.
// Requires ../Common/Structures.hlsl (Mesh_Constants) before it.
#ifndef PE_VISIBILITY_COMMON
#define PE_VISIBILITY_COMMON
#include "../Common/DeformedGeometry.hlsl"

static const uint PE_EDITOR_FLAG_MATERIALX = 32u; // Mesh_Constants.editorFlags: the mesh has a MaterialX material
// A MaterialX material's byte range: the legacy MaterialGpuData record (80 bytes: raster G-buffer, RT hits and the
// off path read it), then the generated module's inputs (pe_load_material).
static const uint PE_OPENPBR_INPUTS_OFFSET = 80u;
static const uint PE_OPENPBR_MODULE_OFFSET = 60u; // MaterialGpuData.transmissionVolume.w: the record's module index

// The module of a draw's MaterialX record, or 0xffffffff when the draw has none.
uint PeModuleOf(ByteAddressBuffer materials, Mesh_Constants mesh)
{
    if ((mesh.editorFlags & PE_EDITOR_FLAG_MATERIALX) == 0u || mesh.materialByteOffset == 0xffffffffu)
        return 0xffffffffu;

    return materials.Load(mesh.materialByteOffset + PE_OPENPBR_MODULE_OFFSET);
}

// One dispatch per module: this pixel's draw is a MaterialX record of the module the dispatch shades.
bool PeMatchesModule(ByteAddressBuffer materials, Mesh_Constants mesh, uint moduleIndex)
{
    return PeModuleOf(materials, mesh) == moduleIndex;
}

// MaterialClassify bins each module's 8x8-pixel tiles: per bin, uint4(pass 1 group counts x, y, z, tile count) and
// uint4(pass 2 group counts x, y, z, unused), then the packed tile coordinates. Pass 1 runs a tile as four 8x8-thread
// groups (two threads per pixel axis), pass 2 as one. A row of either indirect grid holds PE_BIN_ROW_TILES tiles,
// keeping both group counts far under the 65,535 per dimension D3D12 allows.
static const uint PE_BIN_ROW_TILES = 64u;

// The bin's tile at `index` in 8x8-pixel units, or false past its last tile.
bool PeBinTile(ByteAddressBuffer bins, uint countOffset, uint tilesOffset, uint index, out uint2 tile)
{
    tile = uint2(0u, 0u);
    if (index >= bins.Load(countOffset))
        return false;

    const uint packed = bins.Load(tilesOffset + index * 4u);
    tile = uint2(packed & 0xffffu, packed >> 16u);
    return true;
}

// The dispatch thread a binned pass 1 group's thread stands for (the full-screen dispatch's SV_DispatchThreadID), or
// false for the whole group past the bin's last tile.
bool PeBinThread(ByteAddressBuffer bins, uint countOffset, uint tilesOffset, uint3 group, uint3 local, out uint2 tid)
{
    uint2 tile;
    const bool binned = PeBinTile(bins, countOffset, tilesOffset, group.y * PE_BIN_ROW_TILES + (group.x >> 2u), tile);
    tid = tile * 16u + uint2(group.x & 1u, (group.x >> 1u) & 1u) * 8u + local.xy;
    return binned;
}

struct PeSurfacePoint
{
    float4 svPosition; // pixel centre xy, NDC depth, 1/w (what SV_Position holds in a pixel shader)
    float3 P;
    float3 N;
    float3 T;
    float3 B;
    float2 uv0;
    float2 nextUv;
    float spriteBlend;
    float4 color0;
};

struct PeVertex
{
    float3 position;
    float2 uv;
    float2 nextUv;
    float spriteBlend;
    float3 normal;
    float4 tangent;
    float4 color;
};

struct PeTriangle
{
    PeVertex v0;
    PeVertex v1;
    PeVertex v2;
    float4 c0; // clip positions, DepthVS's maths (no viewport Y flip)
    float4 c1;
    float4 c2;
    float4x4 world;
    bool deformed;
};

float4x4 PeLoadMatrix(ByteAddressBuffer data, uint offset)
{
    float4x4 m;
    m[0] = asfloat(data.Load4(offset + 0));
    m[1] = asfloat(data.Load4(offset + 16));
    m[2] = asfloat(data.Load4(offset + 32));
    m[3] = asfloat(data.Load4(offset + 48));
    return m;
}

// Scene vertex store: pe::Vertex, 96 bytes (position 0, uv 12, normals 20, tangent 32, color 48, joints 64, weights 80).
PeVertex PeLoadVertex(ByteAddressBuffer geometry, uint verticesOffset, uint index)
{
    const uint o = verticesOffset + index * 96u;
    PeVertex v;
    v.position = asfloat(geometry.Load3(o + 0u));
    v.uv = asfloat(geometry.Load2(o + 12u));
    const uint4 joints = geometry.Load4(o + 64u);
    const bool sprite = joints.w == SPRITE_FRAME_BLEND_MARKER;
    v.nextUv = sprite ? asfloat(joints.xy) : v.uv;
    v.spriteBlend = sprite ? saturate(asfloat(joints.z)) : 0.0;
    v.normal = asfloat(geometry.Load3(o + 20u));
    v.tangent = asfloat(geometry.Load4(o + 32u));
    v.color = asfloat(geometry.Load4(o + 48u));
    return v;
}

bool PeDeformTriangle(inout PeTriangle t, ByteAddressBuffer deformation, uint4 vis)
{
    PeDeformedVertex d0;
    PeDeformedVertex d1;
    PeDeformedVertex d2;
    if (!PeLoadDeformedVertex(deformation, vis.x - 1u, vis.y, d0) ||
        !PeLoadDeformedVertex(deformation, vis.x - 1u, vis.z, d1) ||
        !PeLoadDeformedVertex(deformation, vis.x - 1u, vis.w, d2))
    {
        return false;
    }

    t.deformed = true;
    t.world = identity_mat;
    t.v0.position = d0.position;
    t.v1.position = d1.position;
    t.v2.position = d2.position;
    t.v0.normal = d0.normal;
    t.v1.normal = d1.normal;
    t.v2.normal = d2.normal;
    t.v0.tangent = d0.tangent;
    t.v1.tangent = d1.tangent;
    t.v2.tangent = d2.tangent;
    t.c0 = d0.clip;
    t.c1 = d1.clip;
    t.c2 = d2.clip;
    return true;
}

// vis = uint4(drawId + 1, v0, v1, v2); frame data: viewProjection at byte 0 (DepthVS's GetViewProjection).
PeTriangle PeLoadTriangle(ByteAddressBuffer geometry, ByteAddressBuffer frameData, ByteAddressBuffer deformation,
                          StructuredBuffer<Mesh_Constants> constants,
                          uint verticesOffset, uint4 vis)
{
    PeTriangle t;
    t.deformed = false;
    const uint id = vis.x - 1u;
    t.world = PeLoadMatrix(frameData, constants[id].meshDataOffset);
    const float4x4 vp = PeLoadMatrix(frameData, 0u);

    t.v0 = PeLoadVertex(geometry, verticesOffset, vis.y);
    t.v1 = PeLoadVertex(geometry, verticesOffset, vis.z);
    t.v2 = PeLoadVertex(geometry, verticesOffset, vis.w);

    if ((constants[id].editorFlags & PE_EDITOR_FLAG_DEFORMED) != 0u && PeDeformTriangle(t, deformation, vis))
    {
        return t;
    }

    const float4x4 final = mul(t.world, vp); // DepthVS: mul(float4(p, 1), mul(world, viewProjection)), precise
    precise float4 c0 = mul(float4(t.v0.position, 1.0), final);
    precise float4 c1 = mul(float4(t.v1.position, 1.0), final);
    precise float4 c2 = mul(float4(t.v2.position, 1.0), final);
    t.c0 = c0;
    t.c1 = c1;
    t.c2 = c2;

    return t;
}

// Framebuffer pixel centre -> unflipped NDC. Vulkan rasterises NDC y = -1 at the top row; DX12 flips clip y in
// ApplyViewportYConvention and rasterises +1 at the top, so both map row y to (y / height) * 2 - 1 here.
float2 PeNdcFromPixel(float2 pixelCentre, float2 size)
{
    return pixelCentre / size * 2.0 - 1.0;
}

float3 PeSafeNormalize(float3 v)
{
    const float len2 = dot(v, v);
    return len2 > 1e-12 ? v * rsqrt(len2) : float3(0.0, 0.0, 0.0);
}

// Perspective-correct barycentrics straight from clip space (homogeneous rasterisation, Olano & Greer 1997): the
// weights b, summing to 1, whose clip point sum(b_i * c_i) projects to ndc. No per-vertex 1/w, so vertices on or
// behind the camera plane stay finite; only an edge-on triangle (never rasterised) is singular.
float3 PeClipBarycentrics(float4 c0, float4 c1, float4 c2, float2 ndc)
{
    const float3 u = float3(c0.x - ndc.x * c0.w, c1.x - ndc.x * c1.w, c2.x - ndc.x * c2.w);
    const float3 v = float3(c0.y - ndc.y * c0.w, c1.y - ndc.y * c1.w, c2.y - ndc.y * c2.w);
    const float3 n = cross(u, v);
    return n / (n.x + n.y + n.z);
}

// The surface at barycentrics b: GBufferVS's world transforms (remove_scale3x3 for normals and tangents) and its
// attributes. svPosition is the caller's (zero for RT hits, which have no pixel).
PeSurfacePoint PeSurfaceAtBarycentrics(PeTriangle t, float3 b)
{
    const float3x3 rot = float3x3(normalize(t.world[0].xyz), normalize(t.world[1].xyz), normalize(t.world[2].xyz));

    PeSurfacePoint sp;

    const float3 pos = t.v0.position * b.x + t.v1.position * b.y + t.v2.position * b.z;
    sp.P = t.deformed ? pos : mul(float4(pos, 1.0), t.world).xyz;

    // Per vertex, as GBufferVS does: transform and normalise, then interpolate, then normalise (GBufferPS).
    const float3 n0 = t.deformed ? t.v0.normal : normalize(mul(t.v0.normal, rot));
    const float3 n1 = t.deformed ? t.v1.normal : normalize(mul(t.v1.normal, rot));
    const float3 n2 = t.deformed ? t.v2.normal : normalize(mul(t.v2.normal, rot));
    sp.N = normalize(n0 * b.x + n1 * b.y + n2 * b.z);

    // Meshes without tangents (PE primitives store zeros) get the triangle's UV tangent dP/du, as MaterialX's own
    // mesh loader generates them; normalising a zero tangent would feed NaN to MaterialX's tangent frame.
    const float3 t0 = t.deformed ? t.v0.tangent.xyz : PeSafeNormalize(mul(t.v0.tangent.xyz, rot));
    const float3 t1 = t.deformed ? t.v1.tangent.xyz : PeSafeNormalize(mul(t.v1.tangent.xyz, rot));
    const float3 t2 = t.deformed ? t.v2.tangent.xyz : PeSafeNormalize(mul(t.v2.tangent.xyz, rot));
    float3 T = t0 * b.x + t1 * b.y + t2 * b.z;
    float sign = t.v0.tangent.w * b.x + t.v1.tangent.w * b.y + t.v2.tangent.w * b.z;
    if (dot(T, T) < 1e-8)
    {
        const float3 e1 = mul(t.v1.position - t.v0.position, (float3x3)t.world);
        const float3 e2 = mul(t.v2.position - t.v0.position, (float3x3)t.world);
        const float2 d1 = t.v1.uv - t.v0.uv;
        const float2 d2 = t.v2.uv - t.v0.uv;
        const float det = d1.x * d2.y - d2.x * d1.y;
        T = (e1 * d2.y - e2 * d1.y) * (det < 0.0 ? -1.0 : 1.0);
        sign = det < 0.0 ? -1.0 : 1.0;
    }

    T -= sp.N * dot(sp.N, T);
    if (dot(T, T) < 1e-12) // no usable UVs either: any tangent perpendicular to N
        T = abs(sp.N.x) < 0.9 ? cross(sp.N, float3(1.0, 0.0, 0.0)) : cross(sp.N, float3(0.0, 1.0, 0.0));
    sp.T = normalize(T);
    sp.B = cross(sp.N, sp.T) * (sign < 0.0 ? -1.0 : 1.0);

    sp.uv0 = t.v0.uv * b.x + t.v1.uv * b.y + t.v2.uv * b.z;
    sp.nextUv = t.v0.nextUv * b.x + t.v1.nextUv * b.y + t.v2.nextUv * b.z;
    // Sprite animation stores one blend for the entire quad; keep graph-evaluation branches quad-uniform.
    sp.spriteBlend = t.v0.spriteBlend;
    sp.color0 = t.v0.color * b.x + t.v1.color * b.y + t.v2.color * b.z;
    sp.svPosition = float4(0.0, 0.0, 0.0, 0.0);

    return sp;
}

// The surface a pixel shader rasterising tri would see at ndc: perspective-correct barycentrics and
// SV_Position = (pixel centre, NDC depth, 1/w).
PeSurfacePoint PeSurfaceAt(PeTriangle t, float2 pixelCentre, float2 size)
{
    const float3 b = PeClipBarycentrics(t.c0, t.c1, t.c2, PeNdcFromPixel(pixelCentre, size));
    PeSurfacePoint sp = PeSurfaceAtBarycentrics(t, b);

    const float clipW = dot(b, float3(t.c0.w, t.c1.w, t.c2.w));
    sp.svPosition = float4(pixelCentre, dot(b, float3(t.c0.z, t.c1.z, t.c2.z)) / clipW, 1.0 / clipW);

    return sp;
}

// Smallest N.V a shading normal may have on a facet that faces the eye: MaterialX's BSDFs flip anything below 0.
static const float PE_MIN_N_DOT_V = 0.01;

// On the silhouette of a coarse mesh the interpolated smooth normal can face away from the eye although the facet faces it.
// MaterialX's BSDFs then flip it toward the eye (mx_forward_facing_normal), which leaves a top-lit limb normal facing away
// from its light: a black pixel. MaterialX's own renderer shows the same dots on a low-poly sphere; a renderer that wants
// the limb lit bends the normal just enough to face the eye instead, as done here. Only a facet that faces the eye is
// bent: the back face of a double-sided mesh keeps MaterialX's flip. The tangent frame follows the normal.
void PeBendLimbNormal(PeTriangle t, float3 V, inout PeSurfacePoint sp)
{
    const float nDotV = dot(sp.N, V);
    if (nDotV >= PE_MIN_N_DOT_V)
        return;

    const float3 e1 = mul(t.v1.position - t.v0.position, (float3x3)t.world);
    const float3 e2 = mul(t.v2.position - t.v0.position, (float3x3)t.world);
    float3 facetNormal = cross(e1, e2);
    if (dot(facetNormal, sp.N) < 0.0)
        facetNormal = -facetNormal;

    if (dot(facetNormal, V) <= 0.0)
        return;

    const float handedness = dot(cross(sp.N, sp.T), sp.B) < 0.0 ? -1.0 : 1.0;
    sp.N = normalize(sp.N + V * (PE_MIN_N_DOT_V - nDotV));

    float3 tangent = sp.T - sp.N * dot(sp.N, sp.T);
    if (dot(tangent, tangent) < 1e-12)
        tangent = abs(sp.N.x) < 0.9 ? cross(sp.N, float3(1.0, 0.0, 0.0)) : cross(sp.N, float3(0.0, 1.0, 0.0));

    sp.T = normalize(tangent);
    sp.B = cross(sp.N, sp.T) * handedness;
}

// The bitangent sign of a triangle's source UV winding (PeSurfaceAt's convention).
float PeUvWindingSign(float2 uv0, float2 uv1, float2 uv2)
{
    const float2 d1 = uv1 - uv0;
    const float2 d2 = uv2 - uv0;
    return d1.x * d2.y - d2.x * d1.y < 0.0 ? -1.0 : 1.0;
}

// A raster pixel's surface (mask and forward passes): interpolated attributes, with PeSurfaceAt's triangle UV tangent
// where the mesh has none. fallbackSign is the source UV winding; only that path reads it.
PeSurfacePoint PeRasterSurface(float4 svPosition,
                               float3 P,
                               float3 N,
                               float4 tangentIn,
                               float2 uv,
                               float2 nextUv,
                               float spriteBlend,
                               float4 color,
                               float fallbackSign)
{
    PeSurfacePoint sp;
    sp.svPosition = svPosition;
    sp.P = P;
    sp.N = normalize(N);
    sp.uv0 = uv;
    sp.nextUv = nextUv;
    sp.spriteBlend = spriteBlend;
    sp.color0 = color;

    float3 tangent = tangentIn.xyz;
    float sign = tangentIn.w;
    const float3 e1 = ddx(sp.P);
    const float3 e2 = ddy(sp.P);
    const float2 d1 = ddx(sp.uv0);
    const float2 d2 = ddy(sp.uv0);
    if (!(dot(tangent, tangent) >= 1e-8)) // also NaN: GBufferVS normalizes a tangent-less mesh's zero tangent
    {
        // The same triangle UV tangent as PeSurfaceAt, derived in this raster's projection.
        const float det = d1.x * d2.y - d2.x * d1.y;
        tangent = (e1 * d2.y - e2 * d1.y) * (det < 0.0 ? -1.0 : 1.0);
        // Screen derivatives recover dP/du, but source UV winding owns the bitangent sign.
        sign = fallbackSign;
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

#endif
