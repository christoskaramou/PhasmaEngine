#pragma once

#include "Material.h"
#include "GLTFSDK/GLTF.h"
#include "GLTFSDK/Document.h"
#include "GLTFSDK/GLTFResourceReader.h"

constexpr auto MAX_NUM_JOINTS = 128u;

namespace pe
{
    class Descriptor;
    class Image;
    class Buffer;
    struct Vertex;
    class CommandBuffer;

    class Primitive
    {
    public:
        Primitive();

        ~Primitive();

        std::string name;

        size_t uniformImagesIndex;
        size_t uniformBufferIndex;
        size_t uniformBufferOffset;

        bool render = true, cull = true;
        uint32_t vertexOffset = 0, indexOffset = 0;
        uint32_t verticesSize = 0, indicesSize = 0;
        PBRMaterial pbrMaterial;
        vec3 min;
        vec3 max;
        vec4 boundingSphere;
        AABB boundingBox;
        vec4 transformedBS;
        bool hasBones = false;

        void CalculateBoundingSphere()
        {
            const vec3 center = (max + min) * .5f;
            const float sphereRadius = length(max - center);
            boundingSphere = vec4(center, sphereRadius);
        }

        void calculateBoundingBox()
        {
            boundingBox.min = min;
            boundingBox.max = max;
        }

        void loadTexture(CommandBuffer *cmd,
                         MaterialType type,
                         const std::filesystem::path &file,
                         const Microsoft::glTF::Image *image = nullptr,
                         const Microsoft::glTF::Document *document = nullptr,
                         const Microsoft::glTF::GLTFResourceReader *resourceReader = nullptr);

        std::vector<Image *> images;
    };

    class Mesh
    {
    public:
        Mesh();

        ~Mesh();

        bool render = true, cull = false;
        std::string name;

        struct MeshData
        {
            mat4 matrix;
            mat4 previousMatrix;
            std::vector<mat4> jointMatrices{};
        } meshData;

        std::vector<Primitive> primitives{};

        size_t uniformBufferIndex;
        size_t uniformBufferOffset;
        std::vector<Vertex> vertices{};
        std::vector<uint32_t> indices{};
        uint32_t vertexOffset = 0, indexOffset = 0;

        void SetUniformOffsets(size_t uniformBufferIndex);

        void destroy();
    };
}