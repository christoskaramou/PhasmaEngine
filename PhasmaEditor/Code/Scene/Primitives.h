#pragma once
#include "API/Vertex.h"

namespace pe
{
    class Model;

    class Primitives
    {
    public:
        static Model *CreatePlane(float width = 10.0f, float depth = 10.0f);
        static Model *CreateCube(float size = 1.0f);
        static Model *CreateSphere(float radius = 1.0f, int slices = 32, int stacks = 32);
        static Model *CreateCylinder(float radius = 1.0f, float height = 2.0f, int slices = 32);
        static Model *CreateCone(float radius = 1.0f, float height = 2.0f, int slices = 32);
        static Model *CreateQuad(float width = 1.0f, float height = 1.0f); // Screen facing

    private:
        static Model *CreatePrimitiveModel(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices);
    };
} // namespace pe
