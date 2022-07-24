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
        m_pipelineGBuffer = nullptr;
        m_pipelineShadows = nullptr;
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
            myPrimitive.pbrMaterial.baseColorFactor = make_vec4(&material.metallicRoughness.baseColorFactor.r);
            myPrimitive.pbrMaterial.doubleSided = material.doubleSided;
            myPrimitive.pbrMaterial.emissiveFactor = make_vec3(&material.emissiveFactor.r);
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
            myPrimitive.min = make_vec3(&accessorPos->min[0]);
            myPrimitive.max = make_vec3(&accessorPos->max[0]);
            myPrimitive.CalculateBoundingSphere();
            myPrimitive.calculateBoundingBox();
            myPrimitive.hasBones = !bonesIDs.empty() && !weights.empty();

            for (size_t i = 0; i < accessorPos->count; i++)
            {
                Vertex vertex{};
                if (!positions.empty())
                    std::copy(positions.begin() + i * 3, positions.begin() + (i * 3 + 3), vertex.position);
                if (!uvs.empty())
                    std::copy(uvs.begin() + i * 2, uvs.begin() + (i * 2 + 2), vertex.uv);
                if (!normals.empty())
                    std::copy(normals.begin() + i * 3, normals.begin() + (i * 3 + 3), vertex.normals);
                if (!colors.empty())
                    std::copy(colors.begin() + i * 4, colors.begin() + (i * 4 + 4), vertex.color);
                if (!bonesIDs.empty())
                    std::copy(bonesIDs.begin() + i * 4, bonesIDs.begin() + (i * 4 + 4), vertex.bonesIDs);
                if (!weights.empty())
                    std::copy(weights.begin() + i * 4, weights.begin() + (i * 4 + 4), vertex.weights);
                myMesh->vertices.push_back(vertex);
            }
            for (auto &index : indices)
            {
                myMesh->indices.push_back(index);
            }

            GUI::loadingCurrent++;
        }
    }

    void CountPrimitives(Microsoft::glTF::Document *document, const glTF::Node &node, uint32_t &count)
    {
        for (auto &child : node.children)
            CountPrimitives(document, document->nodes.Get(child), count);

        if (node.meshId.empty())
            return;

        const auto &mesh = document->meshes.Get(node.meshId);
        for (const auto &primitive : mesh.primitives)
            count++;
    }

    void Model::LoadModelGltf(CommandBuffer *cmd, const std::filesystem::path &file)
    {
        // reads and gets the document and resourceReader objects
        ReadGltf(file);

        uint32_t count = 0;
        for (auto &node : document->GetDefaultScene().nodes)
            CountPrimitives(document, document->nodes.Get(node), count);
        GUI::loadingCurrent = 0;
        GUI::loadingTotal = count;

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
        model.InitRenderTargets();
        model.UpdatePipelineInfo();

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
                    vec3 center = vec3(primitive.boundingSphere);

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

        newNode->translation = make_vec3(&node.translation.x);
        newNode->scale = make_vec3(&node.scale.x);
        newNode->rotation = make_quat(&node.rotation.x);
        newNode->matrix = make_mat4(node.matrix.values.data());

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
                            const vec3 v3 = make_vec3(&data[i * 3]);
                            sampler.outputsVec4.emplace_back(v3, 0.0f);
                        }
                        break;
                    }
                    case glTF::AccessorType::TYPE_VEC4:
                    {
                        for (size_t i = 0; i < accessor.count; i++)
                        {
                            sampler.outputsVec4.emplace_back(make_vec4(&data[i * 4]));
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
        {
            ShadowVertex svert;
            std::copy(vert.position, vert.position + 3, svert.position);
            std::copy(vert.bonesIDs, vert.bonesIDs + 4, svert.bonesIDs);
            std::copy(vert.weights, vert.weights + 4, svert.weights);
            shadowsVertices.push_back(svert);
        }

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

        std::vector<DescriptorBindingInfo> bindingInfos(1);
        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::UniformBufferDynamic;
        uniformBuffer.descriptor = Descriptor::Create(bindingInfos, ShaderStage::VertexBit, "model_uniform_buffer_descriptor");

        uniformBuffer.descriptor->SetBuffer(0, uniformBuffer.buffer);
        uniformBuffer.descriptor->UpdateDescriptor();

        // Map to copy factors in uniform buffer
        MemoryRange mr{};
        uniformBuffer.buffer->Map();

        // mesh dSets
        std::vector<Image *> images{};
        for (auto &node : linearNodes)
        {
            if (!node->mesh)
                continue;

            for (auto &primitive : node->mesh->primitives)
            {
                // this primitive's starting index of images in the uniform array of all images
                primitive.uniformImagesIndex = images.size();

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

                images.push_back(primitive.pbrMaterial.baseColorTexture);
                images.push_back(primitive.pbrMaterial.metallicRoughnessTexture);
                images.push_back(primitive.pbrMaterial.normalTexture);
                images.push_back(primitive.pbrMaterial.occlusionTexture);
                images.push_back(primitive.pbrMaterial.emissiveTexture);
            }
        }

        // Unmap uniform buffer, we are done copying factors
        uniformBuffer.buffer->Flush();
        uniformBuffer.buffer->Unmap();

        std::vector<DescriptorBindingInfo> imageBindingInfos(1);
        imageBindingInfos[0].binding = 0;
        imageBindingInfos[0].count = static_cast<uint32_t>(images.size());
        imageBindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        imageBindingInfos[0].type = DescriptorType::CombinedImageSampler;
        imageBindingInfos[0].bindless = true;
        uniformImages.descriptor = Descriptor::Create(imageBindingInfos, ShaderStage::FragmentBit, "model_images_descriptor");

        uniformImages.descriptor->SetImages(0, images);
        uniformImages.descriptor->UpdateDescriptor();
    }

    void Model::UpdatePipelineInfo()
    {
        UpdatePipelineInfoGBuffer();
        UpdatePipelineInfoShadows();
    }

    void Model::UpdatePipelineInfoGBuffer()
    {
        Format colorformats[]{
            m_normalRT->imageInfo.format,
            m_albedoRT->imageInfo.format,
            m_srmRT->imageInfo.format,
            m_velocityRT->imageInfo.format,
            m_emissiveRT->imageInfo.format};

        Format depthFormat = RHII.GetDepthFormat();

        pipelineInfoGBuffer = std::make_shared<PipelineCreateInfo>();
        PipelineCreateInfo &info = *pipelineInfoGBuffer;

        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Deferred/gBuffer.vert", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/Deferred/gBuffer.frag", ShaderStage::FragmentBit});
        info.vertexInputBindingDescriptions = info.pVertShader->GetReflection().GetVertexBindings();
        info.vertexInputAttributeDescriptions = info.pVertShader->GetReflection().GetVertexAttributes();
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.pushConstantStage = ShaderStage::VertexBit | ShaderStage::FragmentBit;
        info.pushConstantSize = sizeof(Constants);
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
        info.dynamicColorTargets = 5;
        info.colorFormats = colorformats;
        info.depthFormat = &depthFormat;
        info.name = "gbuffer_pipeline";
    }

    void Model::UpdatePipelineInfoShadows()
    {
        auto &uniformBuffer = RHII.GetUniformBufferInfo(uniformBufferIndex);
        Format depthFormat = RHII.GetDepthFormat();

        pipelineInfoShadows = std::make_shared<PipelineCreateInfo>();
        PipelineCreateInfo &info = *pipelineInfoShadows;

        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Shadows/shaderShadows.vert", ShaderStage::VertexBit});
        info.vertexInputBindingDescriptions = info.pVertShader->GetReflection().GetVertexBindings();
        info.vertexInputAttributeDescriptions = info.pVertShader->GetReflection().GetVertexAttributes();
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor, DynamicState::DepthBias};
        info.pushConstantStage = ShaderStage::VertexBit;
        info.pushConstantSize = sizeof(ShadowPushConstData);
        info.colorBlendAttachments = {WORLD_ENTITY->GetComponent<Shadows>()->textures[0]->blendAttachment};
        info.descriptorSetLayouts = {uniformBuffer.descriptor->GetLayout()};
        info.depthFormat = &depthFormat;
        info.name = "shadows_pipeline";
    }

    void CullPrimitiveAsync(Model *model, Mesh *mesh, const Camera &camera, uint32_t index)
    {
        mat4 trans = model->transform * mesh->meshData.matrix;

        vec3 point = trans * vec4(vec3(mesh->primitives[index].boundingSphere), 1.0f);
        float range = mesh->primitives[index].boundingSphere.w * length(vec3(trans[0])); // scale
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
                            vec4 t = mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
                            channel.node->translation = vec3(t);
                            break;
                        }
                        case AnimationPathType::SCALE:
                        {
                            vec4 s = mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
                            channel.node->scale = vec3(s);
                            break;
                        }
                        case AnimationPathType::ROTATION:
                        {
                            quat q1 = make_quat(&sampler.outputsVec4[i].x);
                            quat q2 = make_quat(&sampler.outputsVec4[i + 1].x);
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
        transform = translate(mat4(1.0f), pos) * mat4(quat(radians(rot))) * glm::scale(mat4(1.0f), scale);
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

        pipelineInfoGBuffer->renderPass = WORLD_ENTITY->GetComponent<Deferred>()->GetRenderPassModels();
        cmd->BindPipeline(*pipelineInfoGBuffer, &m_pipelineGBuffer);
        cmd->BindVertexBuffer(vertexBuffer, 0);
        cmd->BindIndexBuffer(indexBuffer, 0);

        Descriptor *dsetHandles[]{
            uniformBuffer.descriptor,
            uniformImages.descriptor};
        cmd->BindDescriptors(m_pipelineGBuffer, 2, dsetHandles);

        Camera *camera = CONTEXT->GetSystem<CameraSystem>()->GetCamera(0);
        m_constants.projJitter[0] = camera->projJitter.x;
        m_constants.projJitter[1] = camera->projJitter.y;
        m_constants.prevProjJitter[0] = camera->prevProjJitter.x;
        m_constants.prevProjJitter[1] = camera->prevProjJitter.y;

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
                                sizeof(Constants),
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

        pipelineInfoShadows->renderPass = shadows.GetRenderPassShadows();
        cmd->BindPipeline(*pipelineInfoShadows, &m_pipelineShadows);
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

    void Model::InitRenderTargets()
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();
        m_normalRT = rs->GetRenderTarget("normal");
        m_albedoRT = rs->GetRenderTarget("albedo");
        m_srmRT = rs->GetRenderTarget("srm");
        m_velocityRT = rs->GetRenderTarget("velocity");
        m_emissiveRT = rs->GetRenderTarget("emissive");
    }

    void Model::Resize()
    {
        InitRenderTargets();
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