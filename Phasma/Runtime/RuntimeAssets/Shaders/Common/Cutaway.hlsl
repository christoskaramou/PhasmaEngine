#ifndef PE_CUTAWAY_HLSL
#define PE_CUTAWAY_HLSL

// The cutaway (phasma::World::SetCutawayVolume, render_graph.set_cutaway): a capsule a script keeps visible. The depth
// prepass discards the pixels of meshes marked for it (Mesh_Constants.editorFlags 512) that stand between the camera
// and the capsule: all of them inside its outline, an ordered 4x4 dither of them across the feather. Fullscreen passes
// receive the same constants (vec4 2 to 4) and call the same functions to tell a hole from the mesh left around it.
static const uint PE_EDITOR_FLAG_CUTAWAY = 512u; // Mesh_Constants.editorFlags: the cutaway may open this mesh

struct PeCutaway
{
    float4 a;      // one end: pixel xy, the depth in front of it, radius in pixels (0: no cutaway)
    float4 b;      // the other end, likewise
    float4 params; // x: feather in pixels, y: dither cells per pixel, z: 1 with reverse depth, -1 without
};

// The share of a marked mesh the cutaway removes at this pixel, [0, 1]. depth is the pixel's own (SV_Position.z).
float PeCutawayCoverage(PeCutaway cut, float2 pixel, float depth)
{
    if (cut.a.w <= 0.0)
        return 0.0;

    const float2 axis = cut.b.xy - cut.a.xy;
    const float t = saturate(dot(pixel - cut.a.xy, axis) / max(dot(axis, axis), 1e-6));
    const float front = lerp(cut.a.z, cut.b.z, t);
    if ((depth - front) * cut.params.z <= 0.0) // level with the capsule or behind it
        return 0.0;

    const float radius = lerp(cut.a.w, cut.b.w, t);
    const float distance = length(pixel - (cut.a.xy + axis * t));
    return 1.0 - smoothstep(radius, radius + max(cut.params.x, 1e-3), distance);
}

static const uint kPeCutawayDither[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

// Whether a marked mesh's pixel with that coverage is discarded. The pattern is fixed to the screen.
bool PeCutawayHole(PeCutaway cut, float2 pixel, float coverage)
{
    const uint2 cell = uint2(pixel * cut.params.y) & 3u;
    return coverage > (float(kPeCutawayDither[cell.y * 4u + cell.x]) + 0.5) / 16.0;
}

#endif
