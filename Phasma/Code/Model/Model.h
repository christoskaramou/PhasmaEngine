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

#include "Core/Math.h"
#include "Script/Script.h"
#include "Systems/CameraSystem.h"
#include "Model/Animation.h"
#include "Core/Node.h"
#include "GLTFSDK/GLTF.h"
#include "GLTFSDK/GLTFResourceReader.h"
#include "GLTFSDK/Document.h"
#include "StreamReader.h"

namespace pe
{
    class Pipeline;

    class CommandBuffer;

    class Descriptor;

    class Buffer;

    class Model : public NoCopy
    {
    public:
        Model();

        ~Model();

        Model &operator=(const Model &other)
        {
            throw std::runtime_error("");
            //return Model();
        }

        // Document holds all info about the gltf model
        Microsoft::glTF::Document *document = nullptr;
        Microsoft::glTF::GLTFResourceReader *resourceReader = nullptr;

        static std::deque <Model> models;
        static Pipeline *pipeline;
        static CommandBuffer *commandBuffer;
        Descriptor *descriptorSet;
        Buffer *uniformBuffer;
        struct UBOModel
        {
            mat4 matrix = mat4::identity();
            mat4 mvp = mat4::identity();
            mat4 previousMvp = mat4::identity();
        } ubo;
        vec3 scale = vec3(1.0f);
        vec3 pos = vec3(0.0f);
        vec3 rot = vec3(0.0f); // euler angles
        mat4 transform = mat4::identity();
        vec4 boundingSphere;
        bool render = true;

        std::string name;
        std::string fullPathName;
        std::vector<pe::Node *> linearNodes{};
        std::vector<Skin *> skins{};
        std::vector <Animation> animations{};
        std::vector <std::string> extensions{};

        int32_t animationIndex = 0;
        float animationTimer = 0.0f;
#if 0
        Script* script = nullptr;
#else
        void *script = nullptr;
#endif

        Buffer *vertexBuffer;
        Buffer *indexBuffer;
        uint32_t numberOfVertices = 0, numberOfIndices = 0;

        void draw(uint16_t alphaMode);

        void update(Camera &camera, double delta);

        void updateAnimation(uint32_t index, float time);

        void calculateBoundingSphere();

        void loadNode(pe::Node *parent, const Microsoft::glTF::Node &node, const std::string &folderPath);

        void loadAnimations();

        void loadSkins();

        void readGltf(const std::filesystem::path &file);

        void loadModelGltf(const std::string &folderPath, const std::string &modelName);

        void getMesh(pe::Node *node, const std::string &meshID, const std::string &folderPath) const;

        template<typename T>
        void getVertexData(std::vector <T> &vec, const std::string &accessorName,
                           const Microsoft::glTF::MeshPrimitive &primitive) const;

        void getIndexData(std::vector <uint32_t> &vec, const Microsoft::glTF::MeshPrimitive &primitive) const;

        Microsoft::glTF::Image *getImage(const std::string &textureID) const;

        void Load(const std::string &folderPath, const std::string &modelName);

        void createVertexBuffer();

        void createIndexBuffer();

        void createUniformBuffers();

        void createDescriptorSets();

        void destroy();
    };
}