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

    // Mesh a PRE-SAMPLED density field of (nx+1)*(ny+1)*(nz+1) corners, indexed
    // i + (nx+1)*(j + (ny+1)*k); corner (i,j,k) sits at world originW + (i,j,k). The field is held by
    // the caller so edits (sculpt) mutate it and re-mesh with no generator re-query. One vertex per
    // surface cell at the mean of its edge zero-crossings; normal = negated field gradient. Empty when
    // no surface crosses the volume.
    SmoothMeshData SurfaceNetsMesh(const std::vector<float> &field, const vec3 &originW,
                                   int nx, int ny, int nz);
} // namespace pe::voxel
