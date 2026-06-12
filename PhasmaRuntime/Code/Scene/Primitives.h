#pragma once
#include "API/Vertex.h"

namespace pe
{
    class ModelAsset;

    class Primitives
    {
    public:
        static ModelAsset *CreatePlane(float width = 10.0f, float depth = 10.0f);
        static ModelAsset *CreateGrid(float width = 10.0f, float depth = 10.0f, int subdivisions = 10);
        static ModelAsset *CreateCube(float size = 1.0f);
        static ModelAsset *CreateSphere(float radius = 1.0f, int slices = 32, int stacks = 32);
        static ModelAsset *CreateUvSphere(float radius = 1.0f, int slices = 32, int stacks = 32);
        static ModelAsset *CreateIcoSphere(float radius = 1.0f, int subdivisions = 2);
        static ModelAsset *CreateCylinder(float radius = 1.0f, float height = 2.0f, int slices = 32);
        static ModelAsset *CreateCone(float radius = 1.0f, float height = 2.0f, int slices = 32);
        static ModelAsset *CreatePyramid(float baseSize = 1.0f, float height = 1.0f);
        static ModelAsset *CreateQuad(float width = 1.0f, float height = 1.0f); // Screen facing
        static ModelAsset *CreateCircle(float radius = 1.0f, int segments = 64);
        static ModelAsset *CreateTorus(float majorRadius = 1.0f, float minorRadius = 0.25f, int majorSegments = 64, int minorSegments = 16);
        // Flat double-sided ribbon along a point path (e.g. orbit lines). Width is in
        // model units; `normal` is the ribbon plane normal the strip expands across.
        static ModelAsset *CreatePolylineRibbon(const std::vector<vec3> &points, float width = 1.0f, const vec3 &normal = vec3(0.0f, 1.0f, 0.0f), bool closed = true);
        // Line-strip centerline (RenderType::Lines + LinesPass): screen-constant 1px
        // hardware lines, visible from any angle and distance.
        static ModelAsset *CreatePolyline(const std::vector<vec3> &points, bool closed = true);
        static ModelAsset *CreateSkinnedStrip2D(float width = 4.0f, float height = 1.0f, int segments = 32, int bones = 24);

    private:
        static ModelAsset *CreatePrimitiveModel(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices, bool lineTopology = false);
    };
} // namespace pe
