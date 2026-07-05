#pragma once
#include "API/Vertex.h"
#include "Base/Math.h"

// Naive Surface Nets: mesh the density==0 isosurface of a scalar field into a smooth triangle mesh
// (standard float Vertex, gradient normals). Unlike the cube GreedyMesher this emits arbitrary
// position/normal geometry, so it renders through the STANDARD mesh path — the packed 8 B VoxelVertex
// (5-bit integer positions, 3-bit face normals) can't hold fractional smooth vertices.
// Convention: density > 0 = solid (below the surface), < 0 = air, 0 = the surface.
namespace pe::voxel
{
    struct SmoothMeshData
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        vec3 aabbMin = vec3(0.0f);
        vec3 aabbMax = vec3(0.0f);
    };

    // Mesh one terrain TILE of the isosurface on a GLOBAL cubic lattice: cell (gx,gy,gz) spans world
    // [g, g+1) * cellSize. Positions and densities derive from global integer corner indices, so two
    // tiles sharing a boundary compute bit-identical vertices there and the seam is watertight.
    //
    // `cells` = scanned cells per axis, starting at global cell `gridMin`. The first `apron` cells per
    // axis (0 or 1; the negative-side overlap into the neighbouring tile) contribute STITCH VERTICES
    // only — quads whose owning edge lies in an apron cell are the neighbour's to emit, which it does
    // with its own bit-identical duplicates of these vertices. Pass apron 0 on axes with no neighbour
    // (world edges, the vertical axis).
    //
    // `field` holds corner densities with a one-corner guard ring on every side: field index (i,j,k),
    // i in [0, cells.x + 3), holds the density at global corner gridMin + (i,j,k) - 1. The guard ring
    // keeps central-difference normals full-stencil at apron vertices (no lighting seam). The caller
    // MUST sample densities at world = float(globalCornerIndex) * cellSize — deriving them from a
    // precomputed float origin drifts by ulps between tiles and hairline-cracks the seam.
    SmoothMeshData SurfaceNetsTile(const std::vector<float> &field, const ivec3 &gridMin,
                                   const ivec3 &cells, const ivec3 &apron, float cellSize);
} // namespace pe::voxel
