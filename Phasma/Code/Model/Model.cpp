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
#include "Renderer/Queue.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"
#include <GLTFSDK/GLBResourceReader.h>
#include <GLTFSDK/Deserialize.h>
#include "Renderer/RHI.h"
#include "Renderer/Vertex.h"
#include "Systems/RendererSystem.h"

#undef max

namespace pe
{
    using namespace Microsoft;

    std::deque<Model> Model::models = {};

    Model::Model()
    {
        uniformBufferIndex = std::numeric_limits<size_t>::max();
        uniformBufferOffset = std::numeric_limits<size_t>::max();
        uniformImagesIndex = std::numeric_limits<size_t>::max();
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

    void Model::ReadGltf(const std::filesystem::path &file)
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
                std::move(streamReader), std::move(gltfStream));
            manifest = resourceReaderGLB->GetJson();
            resourceReader = static_cast<glTF::GLTFResourceReader *>(resourceReaderGLB);
        }

        // std::cout << manifest;

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

    glTF::Image *Model::GetImage(const std::string &textureID) const
    {
        return textureID.empty() ? nullptr : const_cast<glTF::Image *>(&document->images.Get(document->textures.Get(textureID).imageId));
    }

    template <typename T>
    void Model::GetVertexData(std::vector<T> &vec, const std::string &accessorName,
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
                    { return static_cast<T>(value); });
                break;
            }
            case glTF::COMPONENT_BYTE:
            {
                const auto data = resourceReader->ReadBinaryData<int8_t>(doc, accessor);
                vec.resize(data.size());
                std::transform(
                    data.begin(), data.end(), vec.begin(),
                    [](int8_t value) -> T
                    { return static_cast<T>(value); });
                break;
            }
            case glTF::COMPONENT_UNSIGNED_BYTE:
            {
                const auto data = resourceReader->ReadBinaryData<uint8_t>(doc, accessor);
                vec.resize(data.size());
                std::transform(
                    data.begin(), data.end(), vec.begin(),
                    [](uint8_t value) -> T
                    { return static_cast<T>(value); });
                break;
            }
            case glTF::COMPONENT_SHORT:
            {
                const auto data = resourceReader->ReadBinaryData<int16_t>(doc, accessor);
                vec.resize(data.size());
                std::transform(
                    data.begin(), data.end(), vec.begin(),
                    [](int16_t value) -> T
                    { return static_cast<T>(value); });
                break;
            }
            case glTF::COMPONENT_UNSIGNED_SHORT:
            {
                const auto data = resourceReader->ReadBinaryData<uint16_t>(doc, accessor);
                vec.resize(data.size());
                std::transform(
                    data.begin(), data.end(), vec.begin(),
                    [](uint16_t value) -> T
                    { return static_cast<T>(value); });
                break;
            }
            case glTF::COMPONENT_UNSIGNED_INT:
            {
                const auto data = resourceReader->ReadBinaryData<uint32_t>(doc, accessor);
                vec.resize(data.size());
                std::transform(
                    data.begin(), data.end(), vec.begin(),
                    [](uint32_t value) -> T
                    { return static_cast<T>(value); });
                break;
            }
            default:
                PE_ERROR("Unsupported accessor ComponentType");
            }
        }
    }

    void Model::GetIndexData(std::vector<uint32_t> &vec, const glTF::MeshPrimitive &primitive) const
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
                    { return static_cast<uint32_t>(value); });
                break;
            }
            case glTF::COMPONENT_UNSIGNED_BYTE:
            {
                const auto data = resourceReader->ReadBinaryData<uint8_t>(doc, accessor);
                vec.resize(data.size());
                std::transform(
                    data.begin(), data.end(), vec.begin(),
                    [](uint8_t value) -> uint32_t
                    { return static_cast<uint32_t>(value); });
                break;
            }
            case glTF::COMPONENT_SHORT:
            {
                const auto data = resourceReader->ReadBinaryData<int16_t>(doc, accessor);
                vec.resize(data.size());
                std::transform(
                    data.begin(), data.end(), vec.begin(),
                    [](int16_t value) -> uint32_t
                    { return static_cast<uint32_t>(value); });
                break;
            }
            case glTF::COMPONENT_UNSIGNED_SHORT:
            {
                const auto data = resourceReader->ReadBinaryData<uint16_t>(doc, accessor);
                vec.resize(data.size());
                std::transform(
                    data.begin(), data.end(), vec.begin(),
                    [](uint16_t value) -> uint32_t
                    { return static_cast<uint32_t>(value); });
                break;
            }
            case glTF::COMPONENT_UNSIGNED_INT:
            {
                const auto data = resourceReader->ReadBinaryData<uint32_t>(doc, accessor);
                vec.resize(data.size());
                std::transform(
                    data.begin(), data.end(), vec.begin(), [](uint32_t value) -> uint32_t
                    { return value; });
                break;
            }
            default:
                PE_ERROR("Unsupported accessor ComponentType");
            }
        }
    }

    void Model::GetMesh(CommandBuffer *cmd,
                        pe::Node *node,
                        const std::string &meshID,
                        const std::filesystem::path &file) const
    {
        if (!node || meshID.empty())
            return;
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
            std::vector<uint32_t> indices{};

            // ------------ Vertices ------------
            GetVertexData(positions, glTF::ACCESSOR_POSITION, primitive);
            GetVertexData(uvs, glTF::ACCESSOR_TEXCOORD_0, primitive);
            GetVertexData(normals, glTF::ACCESSOR_NORMAL, primitive);
            GetVertexData(colors, glTF::ACCESSOR_COLOR_0, primitive);
            GetVertexData(bonesIDs, glTF::ACCESSOR_JOINTS_0, primitive);
            GetVertexData(weights, glTF::ACCESSOR_WEIGHTS_0, primitive);

            // ------------ Indices ------------
            GetIndexData(indices, primitive);

            // ------------ Materials ------------
            const auto &material = document->materials.Get(primitive.materialId);

            myMesh->primitives.emplace_back();
            auto &myPrimitive = myMesh->primitives.back();

            // factors
            myPrimitive.pbrMaterial.alphaCutoff = material.alphaCutoff;
            myPrimitive.pbrMaterial.renderQueue = (RenderQueue)material.alphaMode;
            myPrimitive.pbrMaterial.baseColorFactor = vec4(&material.metallicRoughness.baseColorFactor.r);
            myPrimitive.pbrMaterial.doubleSided = material.doubleSided;
            myPrimitive.pbrMaterial.emissiveFactor = vec3(&material.emissiveFactor.r);
            myPrimitive.pbrMaterial.metallicFactor = material.metallicRoughness.metallicFactor;
            myPrimitive.pbrMaterial.roughnessFactor = material.metallicRoughness.roughnessFactor;

            // textures
            const auto baseColorImage = GetImage(material.metallicRoughness.baseColorTexture.textureId);
            const auto metallicRoughnessImage = GetImage(material.metallicRoughness.metallicRoughnessTexture.textureId);
            const auto normalImage = GetImage(material.normalTexture.textureId);
            const auto occlusionImage = GetImage(material.occlusionTexture.textureId);
            const auto emissiveImage = GetImage(material.emissiveTexture.textureId);
            myPrimitive.loadTexture(cmd, MaterialType::BaseColor, file, baseColorImage, document, resourceReader);
            myPrimitive.loadTexture(cmd, MaterialType::MetallicRoughness, file, metallicRoughnessImage, document, resourceReader);
            myPrimitive.loadTexture(cmd, MaterialType::Normal, file, normalImage, document, resourceReader);
            myPrimitive.loadTexture(cmd, MaterialType::Occlusion, file, occlusionImage, document, resourceReader);
            myPrimitive.loadTexture(cmd, MaterialType::Emissive, file, emissiveImage, document, resourceReader);

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
            myPrimitive.CalculateBoundingSphere();
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

    void Model::LoadModelGltf(CommandBuffer *cmd, const std::filesystem::path &file)
    {
        // reads and gets the document and resourceReader objects
        ReadGltf(file);

        for (auto &node : document->GetDefaultScene().nodes)
            LoadNode(cmd, nullptr, document->nodes.Get(node), file);
        LoadAnimations();
        LoadSkins();

        for (auto node : linearNodes)
        {
            // Assign skins
            if (node->skinIndex > -1)
            {
                node->skin = skins[node->skinIndex];
                node->mesh->meshData.jointMatrices.resize(node->skin->joints.size());
            }
        }
    }

    void Model::Load(const std::filesystem::path &file)
    {
        Queue *queue = Queue::GetNext(QueueType::GraphicsBit | QueueType::TransferBit, 1);
        CommandBuffer *cmd = CommandBuffer::GetNext(queue->GetFamilyId());
        cmd->Begin();

        Model::models.emplace_back();
        Model &model = Model::models.back();

        // This works as a flag to when the loading is done
        model.render = false;

        model.LoadModelGltf(cmd, file);
        model.name = file.filename().string();
        model.fullPathName = file.string();
        model.CreateVertexBuffer(cmd);
        model.CreateIndexBuffer(cmd);
        model.CreateUniforms();
        model.CreatePipeline();

        model.render = true;

        cmd->End();
        queue->Submit(1, &cmd, nullptr, 0, nullptr, 0, nullptr);

        cmd->Wait();
        CommandBuffer::Return(cmd);

        queue->WaitIdle();
        Queue::Return(queue);
    }

    // position x, y, z and radius w
    void Model::CalculateBoundingSphere()
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

    void Model::LoadNode(CommandBuffer *cmd,
                         pe::Node *parent,
                         const glTF::Node &node,
                         const std::filesystem::path &file)
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

        switch (node.GetTransformationType())
        {
        case glTF::TransformationType::TRANSFORMATION_IDENTITY:
            newNode->transformationType = TransformationType::Identity;
            break;
        case glTF::TransformationType::TRANSFORMATION_MATRIX:
            newNode->transformationType = TransformationType::Matrix;
            break;
        case glTF::TransformationType::TRANSFORMATION_TRS:
            newNode->transformationType = TransformationType::TRS;
            break;
        default:
            PE_ERROR("Node " + node.name + " has Invalid TransformationType");
        }

        newNode->translation = vec3(&node.translation.x);
        newNode->scale = vec3(&node.scale.x);
        newNode->rotation = quat(&node.rotation.x);
        newNode->matrix = mat4(&node.matrix.values[0]);

        // Node with children
        for (auto &child : node.children)
            LoadNode(cmd, newNode, document->nodes.Get(child), file);

        GetMesh(cmd, newNode, node.meshId, file);
        if (parent)
            parent->children.push_back(newNode);
        // else
        //	nodes.push_back(newNode);
        linearNodes.push_back(newNode);
    }

    void Model::LoadAnimations()
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
                    sampler.interpolation = AnimationInterpolationType::LINEAR;
                }
                if (samp.interpolation == glTF::INTERPOLATION_STEP)
                {
                    sampler.interpolation = AnimationInterpolationType::STEP;
                }
                if (samp.interpolation == glTF::INTERPOLATION_CUBICSPLINE)
                {
                    sampler.interpolation = AnimationInterpolationType::CUBICSPLINE;
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
                    channel.path = AnimationPathType::ROTATION;
                }
                if (source.target.path == glTF::TARGET_TRANSLATION)
                {
                    channel.path = AnimationPathType::TRANSLATION;
                }
                if (source.target.path == glTF::TARGET_SCALE)
                {
                    channel.path = AnimationPathType::SCALE;
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

    void Model::LoadSkins()
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
                newSkin->skeletonRoot = getNode(linearNodes, document->nodes.GetIndex(source.skeletonId));

            // Find joint nodes
            for (auto &jointID : source.jointIds)
            {
                Node *node = !jointID.empty() ? getNode(linearNodes, document->nodes.GetIndex(jointID)) : nullptr;
                if (node)
                    newSkin->joints.push_back(node);
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

    void Model::CreateVertexBuffer(CommandBuffer *cmd)
    {
        numberOfVertices = 0;
        for (auto &node : linearNodes)
        {
            if (node->mesh)
                numberOfVertices += static_cast<uint32_t>(node->mesh->vertices.size());
        }

        std::vector<Vertex> vertices{};
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

        auto size = sizeof(Vertex) * numberOfVertices;
        vertexBuffer = Buffer::Create(
            size,
            BufferUsage::TransferDstBit | BufferUsage::VertexBufferBit,
            AllocationCreate::None,
            "model_vertex_buffer");
        cmd->CopyBufferStaged(vertexBuffer, vertices.data(), size, 0);

        // SHADOW VERTEX BUFFER
        std::vector<ShadowVertex> shadowsVertices{};
        shadowsVertices.reserve(numberOfVertices);
        for (auto &vert : vertices)
            shadowsVertices.emplace_back(vert.position, vert.bonesIDs, vert.weights);

        size = sizeof(ShadowVertex) * numberOfVertices;
        shadowsVertexBuffer = Buffer::Create(
            size,
            BufferUsage::TransferDstBit | BufferUsage::VertexBufferBit,
            AllocationCreate::None,
            "model_shadows_vertex_buffer");
        cmd->CopyBufferStaged(shadowsVertexBuffer, shadowsVertices.data(), size, 0);
    }

    void Model::CreateIndexBuffer(CommandBuffer *cmd)
    {
        numberOfIndices = 0;
        for (auto &node : linearNodes)
        {
            if (node->mesh)
                numberOfIndices += static_cast<uint32_t>(node->mesh->indices.size());
        }

        std::vector<uint32_t> indices{};
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
            BufferUsage::TransferDstBit | BufferUsage::IndexBufferBit,
            AllocationCreate::None,
            "model_index_buffer");
        cmd->CopyBufferStaged(indexBuffer, indices.data(), size, 0);
    }

    // Will calculate the buffer size needed for all meshes and their primitives
    void Model::CreateUniforms()
    {
        uniformBufferIndex = RHII.CreateUniformBufferInfo();
        auto &uniformBuffer = RHII.GetUniformBufferInfo(uniformBufferIndex);

        uniformImagesIndex = RHII.CreateUniformImageInfo();
        auto &uniformImages = RHII.GetUniformImageInfo(uniformImagesIndex);

        uniformBufferOffset = uniformBuffer.size;
        uniformBuffer.size += sizeof(UBOModel);

        for (auto &node : linearNodes)
        {
            if (node->mesh)
                node->mesh->SetUniformOffsets(uniformBufferIndex);
        }

        uniformBuffer.buffer = Buffer::Create(
            RHII.AlignUniform(uniformBuffer.size) * SWAPCHAIN_IMAGES,
            BufferUsage::UniformBufferBit,
            AllocationCreate::HostAccessSequentialWriteBit,
            "model_uniform_buffer");

        DescriptorBindingInfo bindingInfo{};
        bindingInfo.binding = 0;
        bindingInfo.pBuffer = uniformBuffer.buffer;
        bindingInfo.type = DescriptorType::UniformBufferDynamic;

        DescriptorInfo info{};
        info.count = 1;
        info.bindingInfos = &bindingInfo;
        info.stage = ShaderStage::VertexBit;

        uniformBuffer.descriptor = Descriptor::Create(&info, "model_uniform_buffer_descriptor");

        // Map to copy factors in uniform buffer
        MemoryRange mr{};
        uniformBuffer.buffer->Map();

        std::vector<DescriptorBindingInfo> bindingInfos{};
        // mesh dSets
        for (auto &node : linearNodes)
        {
            if (!node->mesh)
                continue;

            for (auto &primitive : node->mesh->primitives)
            {
                // this primitive's starting index of images in the uniform array of all images
                primitive.uniformImagesIndex = bindingInfos.size();

                mat4 factors;
                factors[0] = primitive.pbrMaterial.baseColorFactor != vec4(0.f) ? primitive.pbrMaterial.baseColorFactor : vec4(1.f);
                factors[1] = vec4(primitive.pbrMaterial.emissiveFactor, 1.f);
                factors[2] = vec4(
                    primitive.pbrMaterial.metallicFactor, primitive.pbrMaterial.roughnessFactor,
                    primitive.pbrMaterial.alphaCutoff, 0.f);
                factors[3][0] = static_cast<float>(primitive.hasBones);

                mr.data = &factors;
                mr.size = sizeof(factors);
                for (int i = 0; i < SWAPCHAIN_IMAGES; i++)
                {
                    mr.offset = RHII.GetFrameDynamicOffset(uniformBuffer.buffer->Size(), i) + primitive.uniformBufferOffset;
                    uniformBuffer.buffer->Copy(1, &mr, true);
                }

                DescriptorBindingInfo bindingImageInfo{};
                bindingImageInfo.binding = 0;
                bindingImageInfo.type = DescriptorType::CombinedImageSampler;
                bindingImageInfo.imageLayout = ImageLayout::ShaderReadOnly;

                bindingImageInfo.pImage = primitive.pbrMaterial.baseColorTexture;
                bindingInfos.push_back(bindingImageInfo);

                bindingImageInfo.pImage = primitive.pbrMaterial.metallicRoughnessTexture;
                bindingInfos.push_back(bindingImageInfo);

                bindingImageInfo.pImage = primitive.pbrMaterial.normalTexture;
                bindingInfos.push_back(bindingImageInfo);

                bindingImageInfo.pImage = primitive.pbrMaterial.occlusionTexture;
                bindingInfos.push_back(bindingImageInfo);

                bindingImageInfo.pImage = primitive.pbrMaterial.emissiveTexture;
                bindingInfos.push_back(bindingImageInfo);
            }
        }

        // Unmap uniform buffer, we are done copying factors
        uniformBuffer.buffer->Flush();
        uniformBuffer.buffer->Unmap();

        info.count = static_cast<uint32_t>(bindingInfos.size());
        info.bindingInfos = bindingInfos.data();
        info.stage = ShaderStage::FragmentBit;

        uniformImages.descriptor = Descriptor::Create(&info, "model_images_descriptor");
    }

    void Model::CreatePipeline()
    {
        CreatePipelineGBuffer();
        CreatePipelineShadows();
    }

    void Model::CreatePipelineGBuffer()
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();
        m_normalRT = rs->GetRenderTarget("normal");
        m_albedoRT = rs->GetRenderTarget("albedo");
        m_srmRT = rs->GetRenderTarget("srm");
        m_velocityRT = rs->GetRenderTarget("velocity");
        m_emissiveRT = rs->GetRenderTarget("emissive");

        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Deferred/gBuffer.vert", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/Deferred/gBuffer.frag", ShaderStage::FragmentBit});
        info.vertexInputBindingDescriptions = info.pVertShader->GetReflection().GetVertexBindings();
        info.vertexInputAttributeDescriptions = info.pVertShader->GetReflection().GetVertexAttributes();
        info.width = m_albedoRT->width_f;
        info.height = m_albedoRT->height_f;
        info.pushConstantStage = ShaderStage::VertexBit | ShaderStage::FragmentBit;
        info.pushConstantSize = 5 * sizeof(uint32_t);
        info.cullMode = CullMode::Front;
        info.colorBlendAttachments = {
            m_normalRT->blendAttachment,
            m_albedoRT->blendAttachment,
            m_srmRT->blendAttachment,
            m_velocityRT->blendAttachment,
            m_emissiveRT->blendAttachment,
        };
        auto &uniformBuffer = RHII.GetUniformBufferInfo(uniformBufferIndex);
        auto &uniformImages = RHII.GetUniformImageInfo(uniformImagesIndex);
        info.descriptorSetLayouts = {
            uniformBuffer.descriptor->GetLayout(),
            uniformImages.descriptor->GetLayout()};
        info.renderPass = WORLD_ENTITY->GetComponent<Deferred>()->renderPass;
        info.name = "gbuffer_pipeline";

        m_pipelineGBuffer = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
        Shader::Destroy(info.pFragShader);
    }

    void Model::CreatePipelineShadows()
    {
        auto &uniformBuffer = RHII.GetUniformBufferInfo(uniformBufferIndex);

        PipelineCreateInfo info{};

        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Shadows/shaderShadows.vert", ShaderStage::VertexBit});
        info.vertexInputBindingDescriptions = info.pVertShader->GetReflection().GetVertexBindings();
        info.vertexInputAttributeDescriptions = info.pVertShader->GetReflection().GetVertexAttributes();
        info.width = static_cast<float>(SHADOWMAP_SIZE);
        info.height = static_cast<float>(SHADOWMAP_SIZE);
        info.pushConstantStage = ShaderStage::VertexBit;
        info.pushConstantSize = sizeof(ShadowPushConstData);
        info.colorBlendAttachments = {WORLD_ENTITY->GetComponent<Shadows>()->textures[0]->blendAttachment};
        info.dynamicStates = {DynamicState::DepthBias};
        info.descriptorSetLayouts = {uniformBuffer.descriptor->GetLayout()};
        info.renderPass = WORLD_ENTITY->GetComponent<Shadows>()->renderPass;
        info.name = "shadows_pipeline";

        m_pipelineShadows = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);

        // TODO: Follow the same pattern as the deferred pass
        // Begin pass etc.
        // This pipeline should belong to model
    }

    void CullPrimitiveAsync(Model *model, Mesh *mesh, const Camera &camera, uint32_t index)
    {
        mat4 trans = model->transform * mesh->meshData.matrix;

        vec3 point = trans * vec4(vec3(mesh->primitives[index].boundingSphere), 1.0f);
        float range = mesh->primitives[index].boundingSphere.w * abs(trans.scale().x); // scale
        mesh->primitives[index].cull = !camera.PointInFrustum(point, range);
        mesh->primitives[index].transformedBS = vec4(point, range);

        // AABB aabb;
        // aabb.min = trans * vec4(mesh->primitives[index].boundingBox.min, 1.0f);
        // aabb.max = trans * vec4(mesh->primitives[index].boundingBox.max, 1.0f);
        // mesh->primitives[index].cull = !camera.AABBInFrustum(aabb);
    }

    void Model::UpdateAnimation(uint32_t index, float time)
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
                        case AnimationPathType::TRANSLATION:
                        {
                            cvec4 t = mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
                            channel.node->translation = vec3(t);
                            break;
                        }
                        case AnimationPathType::SCALE:
                        {
                            cvec4 s = mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
                            channel.node->scale = vec3(s);
                            break;
                        }
                        case AnimationPathType::ROTATION:
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

    void UpdateNodeAsync(Model *model, Node *node, const Camera &camera)
    {
        if (node->mesh)
        {
            node->Update();

            // async calls should be at least bigger than a number, else this will be slower
            if (node->mesh->primitives.size() > 3)
            {
                std::vector<std::future<void>> futures(node->mesh->primitives.size());

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

    void Model::Update(pe::Camera &camera, double delta)
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

        auto &uniformBuffer = RHII.GetUniformBufferInfo(uniformBufferIndex);
        transform = pe::transform(quat(radians(rot)), scale, pos);
        ubo.matrix = transform;
        ubo.previousMvp = ubo.mvp;
        ubo.mvp = camera.viewProjection * transform;

        MemoryRange mr{};
        mr.data = &ubo;
        mr.size = sizeof(ubo);
        mr.offset = RHII.GetFrameDynamicOffset(uniformBuffer.buffer->Size(), RHII.GetFrameIndex()) + uniformBufferOffset;
        uniformBuffer.buffer->Copy(1, &mr, true);

        if (!animations.empty())
        {
            animationTimer += static_cast<float>(delta);

            if (animationTimer > animations[animationIndex].end)
                animationTimer -= animations[animationIndex].end;

            UpdateAnimation(animationIndex, animationTimer);
        }

        // async calls should be at least bigger than a number, else this will be slower
        if (linearNodes.size() > 3)
        {
            std::vector<std::future<void>> futureNodes(linearNodes.size());

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

    void Model::Draw(CommandBuffer *cmd, RenderQueue renderQueue)
    {
        if (!render)
            return;

        cmd->BeginDebugRegion(
            renderQueue == RenderQueue::Opaque     ? "Opaque"
            : renderQueue == RenderQueue::AlphaCut ? "AlphaCut"
                                                   : "AlphaBlend");

        auto &uniformBuffer = RHII.GetUniformBufferInfo(uniformBufferIndex);
        auto &uniformImages = RHII.GetUniformImageInfo(uniformImagesIndex);

        cmd->BindPipeline(m_pipelineGBuffer);
        cmd->BindVertexBuffer(vertexBuffer, 0);
        cmd->BindIndexBuffer(indexBuffer, 0);

        Descriptor *dsetHandles[]{
            uniformBuffer.descriptor,
            uniformImages.descriptor};
        cmd->BindDescriptors(m_pipelineGBuffer, 2, dsetHandles);

        m_constants.modelIndex = static_cast<uint32_t>(uniformBufferOffset / sizeof(mat4));

        int culled = 0;
        int total = 0;
        for (auto &node : linearNodes)
        {
            if (node->mesh)
            {
                m_constants.meshIndex = static_cast<uint32_t>(node->mesh->uniformBufferOffset / sizeof(mat4));
                m_constants.meshJointCount = static_cast<uint32_t>(node->mesh->meshData.jointMatrices.size());

                for (auto &primitive : node->mesh->primitives)
                {
                    if (primitive.pbrMaterial.renderQueue == renderQueue && primitive.render)
                    {
                        total++;
                        if (!primitive.cull)
                        {
                            m_constants.primitiveIndex = static_cast<uint32_t>(primitive.uniformBufferOffset / sizeof(mat4));
                            m_constants.primitiveImageIndex = static_cast<uint32_t>(primitive.uniformImagesIndex);

                            cmd->PushConstants(
                                m_pipelineGBuffer,
                                m_pipelineGBuffer->info.pushConstantStage,
                                0,
                                sizeof(m_constants),
                                &m_constants);

                            cmd->DrawIndexed(
                                primitive.indicesSize,
                                1,
                                node->mesh->indexOffset + primitive.indexOffset,
                                node->mesh->vertexOffset + primitive.vertexOffset,
                                0);
                        }
                        else
                        {
                            culled++;
                        }
                    }
                }
            }
        }

        cmd->EndDebugRegion();
    }

    void Model::DrawShadow(CommandBuffer *cmd, uint32_t cascade)
    {
        if (!render)
            return;

        Shadows &shadows = *WORLD_ENTITY->GetComponent<Shadows>();
        ShadowPushConstData data;

        cmd->BindPipeline(m_pipelineShadows);
        cmd->BindVertexBuffer(shadowsVertexBuffer, 0);
        cmd->BindIndexBuffer(indexBuffer, 0);

        Descriptor *dset = RHII.GetUniformBufferInfo(uniformBufferIndex).descriptor;
        cmd->BindDescriptors(m_pipelineShadows, 1, &dset);

        for (auto &node : linearNodes)
        {
            if (node->mesh)
            {
                data.cascade = shadows.cascades[cascade] * ubo.matrix * node->mesh->meshData.matrix;
                data.meshIndex = static_cast<uint32_t>(node->mesh->uniformBufferOffset / sizeof(mat4));
                data.meshJointCount = static_cast<uint32_t>(node->mesh->meshData.jointMatrices.size());

                cmd->PushConstants(m_pipelineShadows, ShaderStage::VertexBit, 0, sizeof(ShadowPushConstData), &data);
                for (auto &primitive : node->mesh->primitives)
                {
                    // if (primitive.render)
                    cmd->DrawIndexed(
                        primitive.indicesSize,
                        1,
                        node->mesh->indexOffset + primitive.indexOffset,
                        node->mesh->vertexOffset + primitive.vertexOffset,
                        0);
                }
            }
        }
    }

    void Model::Destroy()
    {
        if (script)
        {
            delete script;
            script = nullptr;
        }

        Pipeline::Destroy(m_pipelineGBuffer);
        Pipeline::Destroy(m_pipelineShadows);

        auto &uniformBuffer = RHII.GetUniformBufferInfo(uniformBufferIndex);
        Buffer::Destroy(uniformBuffer.buffer);
        Descriptor::Destroy(uniformBuffer.descriptor);
        RHII.RemoveUniformBufferInfo(uniformBufferIndex);

        auto &uniformImages = RHII.GetUniformImageInfo(uniformImagesIndex);
        Descriptor::Destroy(uniformImages.descriptor);
        RHII.RemoveUniformImageInfo(uniformImagesIndex);

        delete document;
        delete resourceReader;

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

        Buffer::Destroy(vertexBuffer);
        Buffer::Destroy(shadowsVertexBuffer);
        Buffer::Destroy(indexBuffer);
    }
}