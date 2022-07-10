/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

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

        static std::map<std::string, Image *> uniqueTextures;
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