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

#include "Model/Model.h"
#include "Model/Mesh.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Command.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"
#include <GLTFSDK/GLBResourceReader.h>
#include <GLTFSDK/Deserialize.h>
#include "Renderer/RHI.h"

#undef max

namespace pe
{
    using namespace Microsoft;

    CommandBuffer *Model::commandBuffer = nullptr;
    std::deque <Model> Model::models = {};
    Pipeline *Model::pipeline = nullptr;

    Model::Model()
    {
        descriptorSet = {};
    }

    Model::~Model()
    {
    }

    bool endsWith(const std::string &mainStr, const std::string &toMatch)
    {
        if (mainStr.size() >= toMatch.size() &&
            mainStr.compare(mainStr.size() - toMatch.size(), toMatch.size(), toMatch) == 0)
            return true;
        return false;
    }

    void Model::readGltf(const std::filesystem::path &file)
    {
        if (file.extension() != ".gltf" && file.extension() != ".glb")
            PE_ERROR("Model type not supported");

        std::string manifest;

        // Pass the absolute path, without the filename, to the stream reader
        auto streamReader = std::make_unique<StreamReader>(file.parent_path());
        const std::filesystem::path pathFile = file.filename();
        // Pass a UTF-8 encoded filename to GetInputString
        auto gltfStream = streamReader->GetInputStream(pathFile.string());
        if (file.extension() == ".gltf")
        {
            resourceReader = new glTF::GLTFResourceReader(std::move(streamReader));
            // Read the contents of the glTF file into a std::stringstream
            std::stringstream manifestStream;
            manifestStream << gltfStream->rdbuf();
            manifest = manifestStream.str();
        }
        else
        {
            // GLBResourceReader derives from GLTFResourceReader
            glTF::GLBResourceReader *resourceReaderGLB = new glTF::GLBResourceReader(
                    std::move(streamReader), std::move(gltfStream)
            );
            manifest = resourceReaderGLB->GetJson();
            resourceReader = static_cast<glTF::GLTFResourceReader *>(resourceReaderGLB);
        }

        //std::cout << manifest;

        try
        {
            document = new glTF::Document();
            *document = glTF::Deserialize(manifest);
        }
        catch (const glTF::GLTFException &ex)
        {
            std::stringstream ss;

            ss << "Microsoft::glTF::Deserialize failed: ";
            ss << ex.what();

            PE_ERROR(ss.str());
        }
    }

    glTF::Image *Model::getImage(const std::string &textureID) const
    {
        return textureID.empty() ? nullptr : const_cast<glTF::Image *>(&document->images.Get(
                document->textures.Get(textureID).imageId));
    }

    template<typename T>
    void Model::getVertexData(std::vector <T> &vec, const std::string &accessorName,
                              const glTF::MeshPrimitive &primitive) const
    {
        std::string accessorId;
        if (primitive.TryGetAttributeAccessorId(accessorName, accessorId))
        {
            const auto &doc = *document;
            const auto &accessor = doc.accessors.Get(accessorId);

            switch (accessor.componentType)
            {
            case glTF::COMPONENT_FLOAT:
            {
                const auto data = resourceReader->ReadBinaryData<float>(doc, accessor);
                vec.resize(data.size());
                std::transform(
                        data.begin(), data.end(), vec.begin(),
                        [](float value) -> T
                        { return static_cast<T>(value); }
                );
                break;
            }
            case glTF::COMPONENT_BYTE:
            {
                const auto data = resourceReader->ReadBinaryData<int8_t>(doc, accessor);
                vec.resize(data.size());
                std::transform(
                        data.begin(), data.end(), vec.begin(),
                        [](int8_t value) -> T
                        { return static_cast<T>(value); }
                );
                break;
            }
            case glTF::COMPONENT_UNSIGNED_BYTE:
            {
                const auto data = resourceReader->ReadBinaryData<uint8_t>(doc, accessor);
                vec.resize(data.size());
                std::transform(
                        data.begin(), data.end(), vec.begin(),
                        [](uint8_t value) -> T
                        { return static_cast<T>(value); }
                );
                break;
            }
            case glTF::COMPONENT_SHORT:
            {
                const auto data = resourceReader->ReadBinaryData<int16_t>(doc, accessor);
                vec.resize(data.size());
                std::transform(
                        data.begin(), data.end(), vec.begin(),
                        [](int16_t value) -> T
                        { return static_cast<T>(value); }
                );
                break;
            }
            case glTF::COMPONENT_UNSIGNED_SHORT:
            {
                const auto data = resourceReader->ReadBinaryData<uint16_t>(doc, accessor);
                vec.resize(data.size());
                std::transform(
                        data.begin(), data.end(), vec.begin(),
                        [](uint16_t value) -> T
                        { return static_cast<T>(value); }
                );
                break;
            }
            case glTF::COMPONENT_UNSIGNED_INT:
            {
                const auto data = resourceReader->ReadBinaryData<uint32_t>(doc, accessor);
                vec.resize(data.size());
                std::transform(
                        data.begin(), data.end(), vec.begin(),
                        [](uint32_t value) -> T
                        { return static_cast<T>(value); }
                );
                break;
            }
            default:
                PE_ERROR("Unsupported accessor ComponentType");
            }
        }
    }

    void Model::getIndexData(std::vector <uint32_t> &vec, const glTF::MeshPrimitive &primitive) const
    {
        if (!primitive.indicesAccessorId.empty())
        {
            const auto &doc = *document;
            const auto &accessor = doc.accessors.Get(primitive.indicesAccessorId);

            switch (accessor.componentType)
            {
            case glTF::COMPONENT_BYTE:
            {
                const auto data = resourceReader->ReadBinaryData<int8_t>(doc, accessor);
                vec.resize(data.size());
                std::transform(
                        data.begin(), data.end(), vec.begin(),
                        [](int8_t value) -> uint32_t
                        { return static_cast<uint32_t>(value); }
                );
                break;
            }
            case glTF::COMPONENT_UNSIGNED_BYTE:
            {
                const auto data = resourceReader->ReadBinaryData<uint8_t>(doc, accessor);
                vec.resize(data.size());
                std::transform(
                        data.begin(), data.end(), vec.begin(),
                        [](uint8_t value) -> uint32_t
                        { return static_cast<uint32_t>(value); }
                );
                break;
            }
            case glTF::COMPONENT_SHORT:
            {
                const auto data = resourceReader->ReadBinaryData<int16_t>(doc, accessor);
                vec.resize(data.size());
                std::transform(
                        data.begin(), data.end(), vec.begin(),
                        [](int16_t value) -> uint32_t
                        { return static_cast<uint32_t>(value); }
                );
                break;
            }
            case glTF::COMPONENT_UNSIGNED_SHORT:
            {
                const auto data = resourceReader->ReadBinaryData<uint16_t>(doc, accessor);
                vec.resize(data.size());
                std::transform(
                        data.begin(), data.end(), vec.begin(),
                        [](uint16_t value) -> uint32_t
                        { return static_cast<uint32_t>(value); }
                );
                break;
            }
            case glTF::COMPONENT_UNSIGNED_INT:
            {
                const auto data = resourceReader->ReadBinaryData<uint32_t>(doc, accessor);
                vec.resize(data.size());
                std::transform(
                        data.begin(), data.end(), vec.begin(), [](uint32_t value) -> uint32_t
                        { return value; }
                );
                break;
            }
            default:
                PE_ERROR("Unsupported accessor ComponentType");
            }
        }
    }

    void Model::getMesh(pe::Node *node, const std::string &meshID, const std::filesystem::path &file) const
    {
        if (!node || meshID.empty()) return;
        const auto &mesh = document->meshes.Get(meshID);

        node->mesh = new Mesh();
        auto &myMesh = node->mesh;
        myMesh->name = mesh.name;

        for (const auto &primitive : mesh.primitives)
        {
            std::vector<float> positions{};
            std::vector<float> uvs{};
            std::vector<float> normals{};
            std::vector<float> colors{};
            std::vector<int> bonesIDs{};
            std::vector<float> weights{};
            std::vector <uint32_t> indices{};

            // ------------ Vertices ------------
            getVertexData(positions, glTF::ACCESSOR_POSITION, primitive);
            getVertexData(uvs, glTF::ACCESSOR_TEXCOORD_0, primitive);
            getVertexData(normals, glTF::ACCESSOR_NORMAL, primitive);
            getVertexData(colors, glTF::ACCESSOR_COLOR_0, primitive);
            getVertexData(bonesIDs, glTF::ACCESSOR_JOINTS_0, primitive);
            getVertexData(weights, glTF::ACCESSOR_WEIGHTS_0, primitive);

            // ------------ Indices ------------
            getIndexData(indices, primitive);

            // ------------ Materials ------------
            const auto &material = document->materials.Get(primitive.materialId);

            myMesh->primitives.emplace_back();
            auto &myPrimitive = myMesh->primitives.back();

            // factors
            myPrimitive.pbrMaterial.alphaCutoff = material.alphaCutoff;
            myPrimitive.pbrMaterial.alphaMode = material.alphaMode;
            myPrimitive.pbrMaterial.baseColorFactor = vec4(&material.metallicRoughness.baseColorFactor.r);
            myPrimitive.pbrMaterial.doubleSided = material.doubleSided;
            myPrimitive.pbrMaterial.emissiveFactor = vec3(&material.emissiveFactor.r);
            myPrimitive.pbrMaterial.metallicFactor = material.metallicRoughness.metallicFactor;
            myPrimitive.pbrMaterial.roughnessFactor = material.metallicRoughness.roughnessFactor;

            // textures
            const auto baseColorImage = getImage(material.metallicRoughness.baseColorTexture.textureId);
            const auto metallicRoughnessImage = getImage(material.metallicRoughness.metallicRoughnessTexture.textureId);
            const auto normalImage = getImage(material.normalTexture.textureId);
            const auto occlusionImage = getImage(material.occlusionTexture.textureId);
            const auto emissiveImage = getImage(material.emissiveTexture.textureId);
            myPrimitive.loadTexture(MaterialType::BaseColor, file, baseColorImage, document, resourceReader);
            myPrimitive.loadTexture(MaterialType::MetallicRoughness, file, metallicRoughnessImage, document, resourceReader);
            myPrimitive.loadTexture(MaterialType::Normal, file, normalImage, document, resourceReader);
            myPrimitive.loadTexture(MaterialType::Occlusion, file, occlusionImage, document, resourceReader);
            myPrimitive.loadTexture(MaterialType::Emissive, file, emissiveImage, document, resourceReader);

            if (baseColorImage)
                myPrimitive.name = baseColorImage->name;
            else
                myPrimitive.name = "null";

            std::string accessorId;
            primitive.TryGetAttributeAccessorId(glTF::ACCESSOR_POSITION, accessorId);
            const glTF::Accessor *accessorPos = &document->accessors.Get(accessorId);
            myPrimitive.vertexOffset = static_cast<uint32_t>(myMesh->vertices.size());
            myPrimitive.verticesSize = static_cast<uint32_t>(accessorPos->count);
            myPrimitive.indexOffset = static_cast<uint32_t>(myMesh->indices.size());
            myPrimitive.indicesSize = static_cast<uint32_t>(indices.size());
            myPrimitive.min = vec3(&accessorPos->min[0]);
            myPrimitive.max = vec3(&accessorPos->max[0]);
            myPrimitive.calculateBoundingSphere();
            myPrimitive.calculateBoundingBox();
            myPrimitive.hasBones = !bonesIDs.empty() && !weights.empty();

            for (size_t i = 0; i < accessorPos->count; i++)
            {
                Vertex vertex;
                vertex.position = !positions.empty() ? vec3(&positions[i * 3]) : vec3();
                vertex.uv = !uvs.empty() ? vec2(&uvs[i * 2]) : vec2();
                vertex.normals = !normals.empty() ? vec3(&normals[i * 3]) : vec3();
                vertex.color = !colors.empty() ? vec4(&colors[i * 4]) : vec4();
                vertex.bonesIDs = !bonesIDs.empty() ? ivec4(&bonesIDs[i * 4]) : ivec4();
                vertex.weights = !weights.empty() ? vec4(&weights[i * 4]) : vec4();
                myMesh->vertices.push_back(vertex);
            }
            for (auto &index : indices)
            {
                myMesh->indices.push_back(index);
            }
        }
    }

    void Model::loadModelGltf(const std::filesystem::path &file)
    {
        // reads and gets the document and resourceReader objects
        readGltf(file);

        for (auto &node : document->GetDefaultScene().nodes)
            loadNode({}, document->nodes.Get(node), file);
        loadAnimations();
        loadSkins();

        for (auto node : linearNodes)
        {
            // Assign skins
            if (node->skinIndex > -1)
            {
                node->skin = skins[node->skinIndex];
            }
        }
    }

    void Model::Load(const std::filesystem::path &file)
    {
        RHII.WaitDeviceIdle();
        
        Model::models.emplace_back();
        Model &model = Model::models.back();

        // This works as a flag to when the loading is done
        model.render = false;

        model.loadModelGltf(file);
        //calculateBoundingSphere();
        model.name = file.filename().string();
        model.fullPathName = file.string();
        model.createVertexBuffer();
        model.createIndexBuffer();
        model.createUniformBuffers();
        model.createDescriptorSets();

        model.render = true;
    }

    void Model::updateAnimation(uint32_t index, float time)
    {
        if (index > static_cast<uint32_t>(animations.size()) - 1)
        {
            std::cout << "No animation with index " << index << std::endl;
            return;
        }
        Animation &animation = animations[index];

        for (auto &channel : animation.channels)
        {
            pe::AnimationSampler &sampler = animation.samplers[channel.samplerIndex];
            if (sampler.inputs.size() > sampler.outputsVec4.size())
                continue;

            for (size_t i = 0; i < sampler.inputs.size() - 1; i++)
            {
                if ((time >= sampler.inputs[i]) && (time <= sampler.inputs[i + 1]))
                {
                    const float u =
                            std::max(0.0f, time - sampler.inputs[i]) / (sampler.inputs[i + 1] - sampler.inputs[i]);
                    if (u <= 1.0f)
                    {
                        switch (channel.path)
                        {
                        case pe::AnimationChannel::PathType::TRANSLATION:
                        {
                            cvec4 t = mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
                            channel.node->translation = vec3(t);
                            break;
                        }
                        case pe::AnimationChannel::PathType::SCALE:
                        {
                            cvec4 s = mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
                            channel.node->scale = vec3(s);
                            break;
                        }
                        case pe::AnimationChannel::PathType::ROTATION:
                        {
                            cquat q1(&sampler.outputsVec4[i].x);
                            cquat q2(&sampler.outputsVec4[i + 1].x);
                            channel.node->rotation = normalize(slerp(q1, q2, u));
                            break;
                        }
                        }
                    }
                }
            }
        }
    }

    void CullPrimitiveAsync(Model *model, Mesh *mesh, const Camera &camera, uint32_t index)
    {
        mat4 trans = model->transform * mesh->ubo.matrix;

        vec3 point = trans * vec4(vec3(mesh->primitives[index].boundingSphere), 1.0f);
        float range = mesh->primitives[index].boundingSphere.w * abs(trans.scale().x); // scale
        mesh->primitives[index].cull = !camera.PointInFrustum(point, range);
        mesh->primitives[index].transformedBS = vec4(point, range);

        //AABB aabb;
        //aabb.min = trans * vec4(mesh->primitives[index].boundingBox.min, 1.0f);
        //aabb.max = trans * vec4(mesh->primitives[index].boundingBox.max, 1.0f);
        //mesh->primitives[index].cull = !camera.AABBInFrustum(aabb);
    }

    void UpdateNodeAsync(Model *model, Node *node, const Camera &camera)
    {
        if (node->mesh)
        {
            node->Update();

            // async calls should be at least bigger than a number, else this will be slower
            if (node->mesh->primitives.size() > 3)
            {
                std::vector <std::future<void>> futures(node->mesh->primitives.size());

                for (uint32_t i = 0; i < node->mesh->primitives.size(); i++)
                    futures[i] = std::async(std::launch::async, CullPrimitiveAsync, model, node->mesh, camera, i);

                for (auto &f : futures)
                    f.get();
            }
            else
            {
                for (uint32_t i = 0; i < node->mesh->primitives.size(); i++)
                    CullPrimitiveAsync(model, node->mesh, camera, i);
            }
        }
    }

    void Model::update(pe::Camera &camera, double delta)
    {
        if (!render)
            return;

        if (script)
        {
#if 0
            script->update(static_cast<float>(delta));
            transform = script->getValue<Transform>("transform").matrix * transform;
#endif
        }

        transform = pe::transform(quat(radians(rot)), scale, pos);
        ubo.matrix = transform;
        ubo.previousMvp = ubo.mvp;
        ubo.mvp = camera.viewProjection * transform;

        uniformBuffer->CopyRequest<Launch::AsyncDeferred>({&ubo, sizeof(ubo), 0});

        if (!animations.empty())
        {
            animationTimer += static_cast<float>(delta);

            if (animationTimer > animations[animationIndex].end)
                animationTimer -= animations[animationIndex].end;

            updateAnimation(animationIndex, animationTimer);
        }

        // async calls should be at least bigger than a number, else this will be slower
        if (linearNodes.size() > 3)
        {
            std::vector <std::future<void>> futureNodes(linearNodes.size());

            for (uint32_t i = 0; i < linearNodes.size(); i++)
                futureNodes[i] = std::async(std::launch::async, UpdateNodeAsync, this, linearNodes[i], camera);

            for (auto &f : futureNodes)
                f.get();
        }
        else
        {
            for (auto &linearNode : linearNodes)
                UpdateNodeAsync(this, linearNode, camera);
        }
    }

    void Model::draw(uint16_t alphaMode)
    {
        if (!render || !Model::pipeline)
            return;

        auto &cmd = *Model::commandBuffer;

        cmd.BindPipeline(Model::pipeline);
        cmd.BindVertexBuffer(vertexBuffer, 0);
        cmd.BindIndexBuffer(indexBuffer, 0);

        int culled = 0;
        int total = 0;
        for (auto &node : linearNodes)
        {
            if (node->mesh)
            {
                for (auto &primitive : node->mesh->primitives)
                {
                    if (primitive.pbrMaterial.alphaMode == alphaMode && primitive.render)
                    {
                        total++;
                        if (!primitive.cull)
                        {
                            // Cache this vector
                            std::vector < Descriptor * >
                            dsetHandles{node->mesh->descriptorSet, primitive.descriptorSet, descriptorSet};
                            cmd.BindDescriptors(Model::pipeline, (uint32_t) dsetHandles.size(), dsetHandles.data());
                            cmd.DrawIndexed(
                                    primitive.indicesSize,
                                    1,
                                    node->mesh->indexOffset + primitive.indexOffset,
                                    node->mesh->vertexOffset + primitive.vertexOffset,
                                    0
                            );
                        }
                        else
                        {
                            culled++;
                        }
                    }
                }
            }
        }

        //std::cout << "RenderQueue: " << renderQueue << " Culled: " << culled << "/" << total << std::endl;
    }

    // position x, y, z and radius w
    void Model::calculateBoundingSphere()
    {
        vec4 centerMax(0.f);
        vec4 centerMin(FLT_MAX);

        for (auto &node : linearNodes)
        {
            if (node->mesh)
            {
                for (auto &primitive : node->mesh->primitives)
                {
                    cvec3 center = vec3(primitive.boundingSphere);

                    const float lenMax = length(center) + primitive.boundingSphere.w;
                    if (lenMax > centerMax.w)
                        centerMax = vec4(center, lenMax);

                    const float lenMin = lenMax - 2.f * primitive.boundingSphere.w;
                    if (lenMin < centerMin.w)
                        centerMin = vec4(center, lenMin);
                }
            }
        }
        const vec3 center = (vec3(centerMax) + vec3(centerMin)) * .5f;
        const float sphereRadius = length(vec3(centerMax) - center);
        boundingSphere = vec4(center, sphereRadius);
    }

    void Model::loadNode(pe::Node *parent, const glTF::Node &node, const std::filesystem::path &file)
    {
        pe::Node *newNode = new pe::Node{};
        newNode->index = !node.id.empty() ? static_cast<uint32_t>(document->nodes.GetIndex(node.id)) : -1;
        newNode->parent = parent;
        newNode->name = node.name;
        newNode->skinIndex = !node.skinId.empty() ? static_cast<int32_t>(document->skins.GetIndex(node.skinId)) : -1;

        // Generate local node matrix
        if (!node.HasValidTransformType())
        {
            PE_ERROR("Node " + node.name + " has Invalid TransformType");
            delete newNode;
        }
        newNode->transformationType = static_cast<TransformationType>(node.GetTransformationType());
        newNode->translation = vec3(&node.translation.x);
        newNode->scale = vec3(&node.scale.x);
        newNode->rotation = quat(&node.rotation.x);
        newNode->matrix = mat4(&node.matrix.values[0]);

        // Node with children
        for (auto &child : node.children)
        {
            loadNode(newNode, document->nodes.Get(child), file);
        }
        getMesh(newNode, node.meshId, file);
        if (parent)
            parent->children.push_back(newNode);
        //else
        //	nodes.push_back(newNode);
        linearNodes.push_back(newNode);
    }

    void Model::loadAnimations()
    {
        const auto getNode = [](std::vector<Node *> &linearNodes, size_t index) -> Node *
        {
            for (auto &node : linearNodes)
            {
                if (node->index == index)
                    return node;
            }
            return nullptr;
        };

        for (auto &anim : document->animations.Elements())
        {
            pe::Animation animation{};
            animation.name = anim.name;
            if (anim.name.empty())
            {
                animation.name = std::to_string(animations.size());
            }

            // Samplers
            for (auto &samp : anim.samplers.Elements())
            {
                pe::AnimationSampler sampler{};
                if (samp.interpolation == glTF::INTERPOLATION_LINEAR)
                {
                    sampler.interpolation = AnimationSampler::InterpolationType::LINEAR;
                }
                if (samp.interpolation == glTF::INTERPOLATION_STEP)
                {
                    sampler.interpolation = AnimationSampler::InterpolationType::STEP;
                }
                if (samp.interpolation == glTF::INTERPOLATION_CUBICSPLINE)
                {
                    sampler.interpolation = AnimationSampler::InterpolationType::CUBICSPLINE;
                }
                // Read sampler input time values
                {
                    const glTF::Accessor &accessor = document->accessors.Get(samp.inputAccessorId);
                    if (accessor.componentType != glTF::COMPONENT_FLOAT)
                        PE_ERROR("Animation componentType is not equal to float");
                    const auto data = resourceReader->ReadBinaryData<float>(*document, accessor);
                    sampler.inputs.insert(sampler.inputs.end(), data.begin(), data.end());

                    for (auto input : sampler.inputs)
                    {
                        if (input < animation.start)
                        {
                            animation.start = input;
                        };
                        if (input > animation.end)
                        {
                            animation.end = input;
                        }
                    }
                }
                // Read sampler output T/R/S values
                {
                    const glTF::Accessor &accessor = document->accessors.Get(samp.outputAccessorId);
                    if (accessor.componentType != glTF::COMPONENT_FLOAT)
                        PE_ERROR("Animation componentType is not equal to float");
                    const auto data = resourceReader->ReadBinaryData<float>(*document, accessor);

                    switch (accessor.type)
                    {
                    case glTF::AccessorType::TYPE_VEC3:
                    {
                        for (size_t i = 0; i < accessor.count; i++)
                        {
                            const vec3 v3(&data[i * 3]);
                            sampler.outputsVec4.emplace_back(v3, 0.0f);
                        }
                        break;
                    }
                    case glTF::AccessorType::TYPE_VEC4:
                    {
                        for (size_t i = 0; i < accessor.count; i++)
                        {
                            sampler.outputsVec4.emplace_back(&data[i * 4]);
                        }
                        break;
                    }
                    default:
                    {
                        PE_ERROR("unknown accessor type for TRS");
                    }
                    }
                }
                animation.samplers.push_back(sampler);
            }

            // Channels
            for (auto &source : anim.channels.Elements())
            {
                pe::AnimationChannel channel{};

                if (source.target.path == glTF::TARGET_ROTATION)
                {
                    channel.path = AnimationChannel::PathType::ROTATION;
                }
                if (source.target.path == glTF::TARGET_TRANSLATION)
                {
                    channel.path = AnimationChannel::PathType::TRANSLATION;
                }
                if (source.target.path == glTF::TARGET_SCALE)
                {
                    channel.path = AnimationChannel::PathType::SCALE;
                }
                if (source.target.path == glTF::TARGET_WEIGHTS)
                {
                    std::cout << "weights not yet supported, skipping channel" << std::endl;
                    continue;
                }
                channel.samplerIndex = static_cast<uint32_t>(anim.samplers.GetIndex(source.samplerId));
                channel.node = getNode(linearNodes, document->nodes.GetIndex(source.target.nodeId));
                if (!channel.node)
                {
                    continue;
                }
                animation.channels.push_back(channel);
            }
            animations.push_back(animation);
        }
    }

    void Model::loadSkins()
    {
        const auto getNode = [](std::vector<Node *> &linearNodes, size_t index) -> Node *
        {
            for (auto &node : linearNodes)
            {
                if (node->index == index)
                    return node;
            }
            return nullptr;
        };
        for (auto &source : document->skins.Elements())
        {
            Skin *newSkin = new Skin{};
            newSkin->name = source.name;

            // Find skeleton root node
            if (!source.skeletonId.empty())
            {
                newSkin->skeletonRoot = getNode(linearNodes, document->nodes.GetIndex(source.skeletonId));
            }

            // Find joint nodes
            for (auto &jointID : source.jointIds)
            {
                Node *node = !jointID.empty() ? getNode(linearNodes, document->nodes.GetIndex(jointID)) : nullptr;
                if (node)
                {
                    newSkin->joints.push_back(node);
                }
            }

            // Get inverse bind matrices
            if (!source.inverseBindMatricesAccessorId.empty())
            {
                const glTF::Accessor &accessor = document->accessors.Get(source.inverseBindMatricesAccessorId);
                const auto data = resourceReader->ReadBinaryData<float>(*document, accessor);
                newSkin->inverseBindMatrices.resize(accessor.count);
                memcpy(newSkin->inverseBindMatrices.data(), data.data(), accessor.GetByteLength());
            }
            skins.push_back(newSkin);
        }
    }

    void Model::createVertexBuffer()
    {
        numberOfVertices = 0;
        for (auto &node : linearNodes)
        {
            if (node->mesh)
                numberOfVertices += static_cast<uint32_t>(node->mesh->vertices.size());
        }

        std::vector <Vertex> vertices{};
        vertices.reserve(numberOfVertices);

        for (auto &node : linearNodes)
        {
            if (node->mesh)
            {
                node->mesh->vertexOffset = static_cast<uint32_t>(vertices.size());
                for (auto &vertex : node->mesh->vertices)
                {
                    vertices.push_back(vertex);
                }
            }
        }
        numberOfVertices = static_cast<uint32_t>(vertices.size());
        auto size = sizeof(Vertex) * numberOfVertices;
        vertexBuffer = Buffer::Create(
            size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        // Staging buffer
        Buffer staging(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

        staging.Map();
        staging.CopyData(vertices.data());
        staging.Flush();
        staging.Unmap();

        vertexBuffer->CopyBuffer(&staging, staging.Size());
        staging.Destroy();
    }

    void Model::createIndexBuffer()
    {
        numberOfIndices = 0;
        for (auto &node : linearNodes)
        {
            if (node->mesh)
                numberOfIndices += static_cast<uint32_t>(node->mesh->indices.size());
        }

        std::vector <uint32_t> indices{};
        indices.reserve(numberOfIndices);

        for (auto &node : linearNodes)
        {
            if (node->mesh)
            {
                node->mesh->indexOffset = static_cast<uint32_t>(indices.size());
                for (auto &index : node->mesh->indices)
                {
                    indices.push_back(index);
                }
            }
        }

        auto size = sizeof(uint32_t) * numberOfIndices;
        indexBuffer = Buffer::Create(
            size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        // Staging buffer
        Buffer *staging = Buffer::Create(
            size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_ONLY
        );
        staging->Map();
        staging->CopyData(indices.data());
        staging->Flush();
        staging->Unmap();

        indexBuffer->CopyBuffer(staging, staging->Size());
        staging->Destroy();
    }

    void Model::createUniformBuffers()
    {
        uniformBuffer = Buffer::Create(
            sizeof(ubo),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );
        uniformBuffer->Map();
        uniformBuffer->Zero();
        uniformBuffer->Flush();
        uniformBuffer->Unmap();
        for (auto &node : linearNodes)
        {
            if (node->mesh)
            {
                node->mesh->createUniformBuffers();
            }
        }
    }

    void Model::createDescriptorSets()
    {
        // model dSet
        descriptorSet = Descriptor::Create(Pipeline::getDescriptorSetLayoutModel());

        DescriptorUpdateInfo info{};
        info.binding = 0;
        info.pBuffer = uniformBuffer;;
        descriptorSet->UpdateDescriptor(1, &info);

        // mesh dSets
        for (auto &node : linearNodes)
        {
            if (!node->mesh)
                continue;

            auto &mesh = node->mesh;

            mesh->descriptorSet = Descriptor::Create(Pipeline::getDescriptorSetLayoutMesh());

            DescriptorUpdateInfo info{};
            info.binding = 0;
            info.pBuffer = mesh->uniformBuffer;
            mesh->descriptorSet->UpdateDescriptor(1, &info);

            // primitive dSets
            for (auto &primitive : mesh->primitives)
            {
                primitive.descriptorSet = Descriptor::Create(Pipeline::getDescriptorSetLayoutPrimitive());

                std::array<DescriptorUpdateInfo, 6> infos{};

                infos[0].binding = 0;
                infos[0].pImage = primitive.pbrMaterial.baseColorTexture;

                infos[1].binding = 1;
                infos[1].pImage = primitive.pbrMaterial.metallicRoughnessTexture;

                infos[2].binding = 2;
                infos[2].pImage = primitive.pbrMaterial.normalTexture;

                infos[3].binding = 3;
                infos[3].pImage = primitive.pbrMaterial.occlusionTexture;

                infos[4].binding = 4;
                infos[4].pImage = primitive.pbrMaterial.emissiveTexture;

                infos[5].binding = 5;
                infos[5].pBuffer = primitive.uniformBuffer;

                primitive.descriptorSet->UpdateDescriptor(6, infos.data());
            }
        }
    }

    void Model::destroy()
    {
        if (script)
        {
            delete script;
            script = nullptr;
        }
        uniformBuffer->Destroy();
        delete document;
        delete resourceReader;
        Pipeline::getDescriptorSetLayoutModel()->Destroy();
        for (auto &node : linearNodes)
        {
            if (node->mesh)
            {
                node->mesh->destroy();
                delete node->mesh;
                node->mesh = {};
            }
            delete node;
            node = {};
        }
        for (auto &skin : skins)
        {
            delete skin;
            skin = {};
        }
        //for (auto& texture : Mesh::uniqueTextures)
        //	texture.second.destroy();
        //Mesh::uniqueTextures.clear();
        vertexBuffer->Destroy();
        indexBuffer->Destroy();
    }
}