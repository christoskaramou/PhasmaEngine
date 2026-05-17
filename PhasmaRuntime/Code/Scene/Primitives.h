#pragma once
#include "API/Vertex.h"

namespace pe
{
    class ModelAsset;

    class Primitives
    {
    public:
        static ModelAsset *CreatePlane(float width = 10.0f, float depth = 10.0f);
        static ModelAsset *CreateCube(float size = 1.0f);
        static ModelAsset *CreateSphere(float radius = 1.0f, int slices = 32, int stacks = 32);
        static ModelAsset *CreateCylinder(float radius = 1.0f, float height = 2.0f, int slices = 32);
        static ModelAsset *CreateCone(float radius = 1.0f, float height = 2.0f, int slices = 32);
        static ModelAsset *CreateQuad(float width = 1.0f, float height = 1.0f); // Screen facing

    private:
        static ModelAsset *CreatePrimitiveModel(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices);
    };
} // namespace pe
