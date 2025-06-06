#include "Scene/Model.h"
#include "API/Pipeline.h"
#include "API/Queue.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Buffer.h"
#include "API/RenderPass.h"
#include "API/RHI.h"
#include "Systems/RendererSystem.h"
#include "Scene/Scene.h"
#include "Camera/Camera.h"

#undef max

namespace pe
{
    Image *g_defaultBlack = nullptr;
    Image *g_defaultNormal = nullptr;
    Image *g_defaultWhite = nullptr;
    Sampler *g_defaultSampler = nullptr;

    ModelGltf::ModelGltf() : m_id{ID::NextID()}
    {
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
            dirtyUniforms[i] = false;
    }

    ModelGltf::~ModelGltf()
    {
        for (auto image : m_images)
            Image::Destroy(image);
    }

    ModelGltf *ModelGltf::Load(const std::filesystem::path &file)
    {
        PE_ERROR_IF(!std::filesystem::exists(file), "Model file not found: " + file.string());

        ModelGltf *modelGltf = new ModelGltf();
        ModelGltf &model = *modelGltf;

        tinygltf::TinyGLTF loader;
        std::string err, warn;

        auto &gSettings = Settings::Get<GlobalSettings>();
        gSettings.loading_name = "Reading from file";

        if (!model.LoadFile(file, loader, err, warn))
            PE_ERROR("Failed to load model: " + file.string());

        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->GetCommandBuffer(CommandPoolCreate::TransientBit);

        cmd->Begin();
        model.UploadImages(cmd);
        model.CreateSamplers();
        model.ExtractMaterialInfo();
        model.ProcessAabbs();
        model.AcquireGeometryInfo();
        model.SetupNodes();
        model.UpdateAllNodeMatrices();
        model.UploadBuffers(cmd);
        cmd->End();

        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();

        modelGltf->m_render = true;

        return modelGltf;
    }

    bool ModelGltf::LoadFile(const std::filesystem::path &file, tinygltf::TinyGLTF &loader, std::string &err, std::string &warn)
    {
        if (file.extension() == ".gltf")
            return loader.LoadASCIIFromFile(this, &err, &warn, file.string());
        else if (file.extension() == ".glb")
            return loader.LoadBinaryFromFile(this, &err, &warn, file.string());
        return false;
    }

    void ModelGltf::UploadImages(CommandBuffer *cmd)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading_current;
        auto &total = gSettings.loading_total;
        auto &loading = gSettings.loading_name;

        if (!g_defaultBlack)
            g_defaultBlack = Image::LoadRGBA8(cmd, Path::Executable + "Assets/Objects/black.png");
        if (!g_defaultNormal)
            g_defaultNormal = Image::LoadRGBA8(cmd, Path::Executable + "Assets/Objects/normal.png");
        if (!g_defaultWhite)
            g_defaultWhite = Image::LoadRGBA8(cmd, Path::Executable + "Assets/Objects/white.png");
        if (!g_defaultSampler)
        {
            SamplerCreateInfo info{};
            info.name = "Default Sampler";
            info.UpdateHash();
            g_defaultSampler = Sampler::Create(info);
        }

        m_images.reserve(images.size() + 3);
        for (auto &image : images)
        {
            PE_ERROR_IF(image.image.empty(), "Image data is empty: " + image.uri);

            ImageCreateInfo info{};
            info.format = Format::RGBA8Unorm;
            info.mipLevels = Image::CalculateMips(image.width, image.height);
            info.width = image.width;
            info.height = image.height;
            info.usage = ImageUsage::TransferSrcBit | ImageUsage::TransferDstBit | ImageUsage::SampledBit | ImageUsage::StorageBit;
            info.initialLayout = ImageLayout::Preinitialized;
            info.name = image.uri;

            Image *uploadImage = Image::Create(info);
            uploadImage->CreateSRV(ImageViewType::Type2D);
            PE_ERROR_IF(uploadImage->GetSRV().IsNull(), "Failed to create SRV for image: " + image.uri);

            void *data = image.image.data();
            size_t size = static_cast<uint32_t>(image.image.size());
            cmd->CopyDataToImageStaged(uploadImage, data, size);
            cmd->GenerateMipMaps(uploadImage);

            ImageBarrierInfo barrier{};
            barrier.image = uploadImage;
            barrier.layout = ImageLayout::ShaderReadOnly;
            barrier.stageFlags = PipelineStage::FragmentShaderBit;
            barrier.accessMask = Access::ShaderReadBit;
            cmd->ImageBarrier(barrier);

            m_images.push_back(uploadImage);

            // Update loading bar
            progress++;
        }
        m_images.push_back(g_defaultBlack);
        m_images.push_back(g_defaultNormal);
        m_images.push_back(g_defaultWhite);
    }

    void ModelGltf::CreateSamplers()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading_current;
        auto &total = gSettings.loading_total;
        auto &loading = gSettings.loading_name;

        m_samplers.reserve(samplers.size() + 1);
        for (auto &sampler : samplers)
        {
            SamplerCreateInfo info{};
            info.name = sampler.name;
            if (sampler.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST)
            {
                info.minFilter = Filter::Nearest;
            }
            else if (sampler.minFilter == TINYGLTF_TEXTURE_FILTER_LINEAR)
            {
                info.minFilter = Filter::Linear;
            }
            else if (sampler.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST)
            {
                info.minFilter = Filter::Nearest;
                info.mipmapMode = SamplerMipmapMode::Nearest;
            }
            else if (sampler.minFilter == TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST)
            {
                info.minFilter = Filter::Linear;
                info.mipmapMode = SamplerMipmapMode::Nearest;
            }
            else if (sampler.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR)
            {
                info.minFilter = Filter::Nearest;
                info.mipmapMode = SamplerMipmapMode::Linear;
            }
            else if (sampler.minFilter == TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR)
            {
                info.minFilter = Filter::Linear;
                info.mipmapMode = SamplerMipmapMode::Linear;
            }
            else
            {
                info.minFilter = Filter::Linear;
                info.mipmapMode = SamplerMipmapMode::Linear;
            }

            if (sampler.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST)
                info.magFilter = Filter::Nearest;
            else
                info.magFilter = Filter::Linear;

            if (sampler.wrapS == TINYGLTF_TEXTURE_WRAP_REPEAT)
                info.addressModeU = SamplerAddressMode::Repeat;
            else if (sampler.wrapS == TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE)
                info.addressModeU = SamplerAddressMode::ClampToEdge;
            else if (sampler.wrapS == TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT)
                info.addressModeU = SamplerAddressMode::MirroredRepeat;

            if (sampler.wrapT == TINYGLTF_TEXTURE_WRAP_REPEAT)
                info.addressModeV = SamplerAddressMode::Repeat;
            else if (sampler.wrapT == TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE)
                info.addressModeV = SamplerAddressMode::ClampToEdge;
            else if (sampler.wrapT == TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT)
                info.addressModeV = SamplerAddressMode::MirroredRepeat;

            info.UpdateHash();
            Sampler *uploadSampler = Sampler::Create(info);
            m_samplers.push_back(uploadSampler);
            m_samplersMap[info.GetHash()] = uploadSampler; // take as granted that samplers are not duplicated

            // Update loading bar
            progress++;
        }
        m_samplers.push_back(g_defaultSampler);
        m_samplersMap[g_defaultSampler->GetInfo().GetHash()] = g_defaultSampler;
    }

    void ModelGltf::ExtractMaterialInfo()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading_current;
        auto &total = gSettings.loading_total;
        auto &loading = gSettings.loading_name;

        progress = 0;
        for (size_t i = 0; i < meshes.size(); i++)
        {
            tinygltf::Mesh &mesh = meshes[i];
            for (size_t j = 0; j < mesh.primitives.size(); j++)
                m_primitivesCount++;
        }
        total = m_primitivesCount;
        loading = "Loading material pipeline info";

        // Material info for pipeline creation
        m_meshesInfo.resize(meshes.size());
        for (size_t i = 0; i < meshes.size(); i++)
        {
            tinygltf::Mesh &mesh = meshes[i];
            MeshInfo &meshInfo = m_meshesInfo[i];
            meshInfo.primitivesInfo.resize(mesh.primitives.size());

            for (size_t j = 0; j < mesh.primitives.size(); j++)
            {
                tinygltf::Primitive &primitive = mesh.primitives[j];
                PrimitiveInfo &primitiveInfo = meshInfo.primitivesInfo[j];

                tinygltf::Material &material = materials[primitive.material];
                MaterialInfo &materialInfo = primitiveInfo.materialInfo;

                switch (primitive.mode)
                {
                case TINYGLTF_MODE_POINTS:
                    materialInfo.topology = PrimitiveTopology::PointList;
                    break;
                case TINYGLTF_MODE_LINE:
                    materialInfo.topology = PrimitiveTopology::LineList;
                    break;
                case TINYGLTF_MODE_LINE_STRIP:
                    materialInfo.topology = PrimitiveTopology::LineStrip;
                    break;
                case TINYGLTF_MODE_TRIANGLES:
                    materialInfo.topology = PrimitiveTopology::TriangleList;
                    break;
                case TINYGLTF_MODE_TRIANGLE_STRIP:
                    materialInfo.topology = PrimitiveTopology::TriangleStrip;
                    break;
                case TINYGLTF_MODE_TRIANGLE_FAN:
                    materialInfo.topology = PrimitiveTopology::TriangleFan;
                    break;
                default:
                    PE_ERROR("Invalid primitive mode");
                    break;
                }
                materialInfo.cullMode = material.doubleSided ? CullMode::None : CullMode::Front;
                materialInfo.alphaBlend = material.alphaMode == "BLEND" || material.alphaMode == "MASK";

                // Update loading bar
                progress++;
            }
        }
    }

    void ModelGltf::ProcessAabbs()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading_current;
        auto &total = gSettings.loading_total;
        auto &loading = gSettings.loading_name;

        progress = 0;
        total = m_primitivesCount;
        loading = "Loading aabbs";

        m_primitivesCount = 0;
        for (int i = 0; i < meshes.size(); i++)
        {
            tinygltf::Mesh &mesh = meshes[i];
            MeshInfo &meshInfo = m_meshesInfo[i];
            for (int j = 0; j < mesh.primitives.size(); j++)
            {
                auto posIt = mesh.primitives[j].attributes.find("POSITION");
                const auto &accessorPos = accessors[posIt->second];
                const auto &accessorIndices = accessors[mesh.primitives[j].indices];

                PrimitiveInfo &primitiveInfo = meshInfo.primitivesInfo[j];
                primitiveInfo.vertexOffset = m_verticesCount;
                primitiveInfo.verticesCount = static_cast<uint32_t>(accessorPos.count);
                primitiveInfo.indexOffset = m_indicesCount;
                primitiveInfo.indicesCount = static_cast<uint32_t>(accessorIndices.count);
                primitiveInfo.aabbVertexOffset = m_primitivesCount * 8;
                primitiveInfo.boundingBox.min.x = static_cast<float>(accessorPos.minValues[0]);
                primitiveInfo.boundingBox.min.y = static_cast<float>(accessorPos.minValues[1]);
                primitiveInfo.boundingBox.min.z = static_cast<float>(accessorPos.minValues[2]);
                primitiveInfo.boundingBox.max.x = static_cast<float>(accessorPos.maxValues[0]);
                primitiveInfo.boundingBox.max.y = static_cast<float>(accessorPos.maxValues[1]);
                primitiveInfo.boundingBox.max.z = static_cast<float>(accessorPos.maxValues[2]);

                m_verticesCount += static_cast<uint32_t>(accessorPos.count);
                m_indicesCount += static_cast<uint32_t>(accessorIndices.count);
                m_primitivesCount++;

                // Update loading bar
                progress++;
            }
        }
    }

    void ModelGltf::AcquireGeometryInfo()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading_current;
        auto &total = gSettings.loading_total;
        auto &loading = gSettings.loading_name;

        progress = 0;
        total = m_verticesCount + m_indicesCount;
        loading = "Loading vertices and indices";

        m_vertices.reserve(m_verticesCount);
        m_positionUvs.reserve(m_verticesCount);
        m_aabbVertices.reserve(m_primitivesCount * 8);
        m_indices.reserve(m_indicesCount);

        size_t indexOffset = 0;

        for (uint32_t i = 0; i < meshes.size(); i++)
        {
            tinygltf::Mesh &mesh = meshes[i];
            MeshInfo &meshInfo = m_meshesInfo[i];

            for (uint32_t j = 0; j < mesh.primitives.size(); j++)
            {
                tinygltf::Primitive &primitive = mesh.primitives[j];
                PrimitiveInfo &primitiveInfo = meshInfo.primitivesInfo[j];
                tinygltf::Material &material = materials[primitive.material];

                // -------- Alpha Mode --------
                if (material.alphaMode == "BLEND")
                    primitiveInfo.renderType = RenderType::AlphaBlend;
                else if (material.alphaMode == "MASK")
                    primitiveInfo.renderType = RenderType::AlphaCut;
                else if (material.alphaMode == "OPAQUE")
                    primitiveInfo.renderType = RenderType::Opaque;
                else
                    PE_ERROR("Invalid alphaMode type");

                // -------- Image & Sampler Setup --------
                auto setTexture = [this, &primitiveInfo](TextureType type, int gltfIndex, Image *defaultImage)
                {
                    Image *image = defaultImage;
                    Sampler *sampler = g_defaultSampler;

                    if (gltfIndex >= 0)
                    {
                        const auto &texture = textures[gltfIndex];
                        if (texture.source >= 0)
                            image = m_images[texture.source];
                        if (texture.sampler >= 0)
                            sampler = m_samplers[texture.sampler];
                    }

                    primitiveInfo.images[static_cast<int>(type)] = image;
                    primitiveInfo.samplers[static_cast<int>(type)] = sampler;
                };
                setTexture(TextureType::BaseColor, material.pbrMetallicRoughness.baseColorTexture.index, g_defaultBlack);
                setTexture(TextureType::MetallicRoughness, material.pbrMetallicRoughness.metallicRoughnessTexture.index, g_defaultBlack);
                setTexture(TextureType::Normal, material.normalTexture.index, g_defaultNormal);
                setTexture(TextureType::Occlusion, material.occlusionTexture.index, g_defaultWhite);
                setTexture(TextureType::Emissive, material.emissiveTexture.index, g_defaultBlack);

                // -------- Attribute Extraction --------
                auto getAccessor = [this, &primitive](const std::string &attrName) -> const tinygltf::Accessor *
                {
                    auto attrIt = primitive.attributes.find(attrName);
                    if (attrIt == primitive.attributes.end())
                        return nullptr;

                    return &accessors[attrIt->second];
                };

                auto getData = [this, &primitive](const std::string &attrName) -> const uint8_t *
                {
                    auto attrIt = primitive.attributes.find(attrName);
                    if (attrIt == primitive.attributes.end())
                        return nullptr;

                    auto &accessor = accessors[attrIt->second];
                    if (accessor.bufferView < 0)
                        return nullptr;

                    auto &bufferView = bufferViews[accessor.bufferView];
                    if (bufferView.buffer < 0)
                        return nullptr;

                    auto &buffer = buffers[bufferView.buffer];
                    if (buffer.data.empty())
                        return nullptr;

                    size_t offset = accessor.byteOffset + bufferView.byteOffset;
                    if (offset >= buffer.data.size())
                        return nullptr;

                    return &buffer.data[accessor.byteOffset + bufferView.byteOffset];
                };

                const auto *dataPos = reinterpret_cast<const float *>(getData("POSITION"));
                const auto *dataUV0 = reinterpret_cast<const float *>(getData("TEXCOORD_0"));
                const auto *dataUV1 = reinterpret_cast<const float *>(getData("TEXCOORD_1"));
                const auto *dataNormal = reinterpret_cast<const float *>(getData("NORMAL"));
                const auto *dataColor = reinterpret_cast<const float *>(getData("COLOR_0"));
                const auto *dataJoints = reinterpret_cast<const void *>(getData("JOINTS_0"));
                const auto *dataWeights = reinterpret_cast<const float *>(getData("WEIGHTS_0"));

                auto accessorPos = getAccessor("POSITION");
                PE_ERROR_IF(!accessorPos, "No POSITION attribute found in primitive");

                for (uint32_t i = 0; i < accessorPos->count; i++)
                {
                    Vertex vertex{};
                    PositionUvVertex positionUvVertex{};

                    std::copy(dataPos + i * 3, dataPos + (i * 3 + 3), vertex.position);
                    std::copy(dataPos + i * 3, dataPos + (i * 3 + 3), positionUvVertex.position);

                    if (dataUV0)
                        std::copy(dataUV0 + i * 2, dataUV0 + (i * 2 + 2), vertex.uv);

                    if (dataNormal)
                        std::copy(dataNormal + i * 3, dataNormal + (i * 3 + 3), vertex.normals);
                    else
                        vertex.normals[2] = 1.f;

                    if (dataColor)
                        std::copy(dataColor + i * 4, dataColor + (i * 4 + 4), vertex.color);
                    else
                        std::fill(vertex.color, vertex.color + 4, 1.f);

                    if (dataJoints)
                    {
                        auto *accessorJoints = getAccessor("JOINTS_0");
                        if (accessorJoints->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                        {
                            const uint8_t *dataJoints8 = reinterpret_cast<const uint8_t *>(dataJoints);
                            for (uint32_t j = 0; j < 4; j++)
                                vertex.joints[j] = dataJoints8[i * 4 + j];
                        }
                        else if (accessorJoints->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                        {
                            const uint16_t *dataJoints16 = reinterpret_cast<const uint16_t *>(dataJoints);
                            for (uint32_t j = 0; j < 4; j++)
                                vertex.joints[j] = dataJoints16[i * 4 + j];
                        }
                    }

                    if (dataWeights)
                        std::copy(dataWeights + i * 4, dataWeights + (i * 4 + 4), vertex.weights);

                    m_vertices.push_back(vertex);
                    m_positionUvs.push_back(positionUvVertex);
                    progress++;
                }

                // -------- Indices --------
                if (primitive.indices >= 0)
                {
                    const auto &accessorIndices = accessors[primitive.indices];
                    const auto &bufferViewIndices = bufferViews[accessorIndices.bufferView];
                    const auto &bufferIndices = buffers[bufferViewIndices.buffer];
                    const auto dataPtr = &bufferIndices.data[accessorIndices.byteOffset + bufferViewIndices.byteOffset];

                    switch (accessorIndices.componentType)
                    {
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                    {
                        const auto dataIndices = reinterpret_cast<const uint16_t *>(dataPtr);
                        for (size_t i = 0; i < accessorIndices.count; i++)
                            m_indices.push_back(static_cast<uint32_t>(dataIndices[i]));
                        break;
                    }
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                    {
                        const auto dataIndices = reinterpret_cast<const uint32_t *>(dataPtr);
                        for (size_t i = 0; i < accessorIndices.count; i++)
                            m_indices.push_back(dataIndices[i]);
                        break;
                    }
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                    {
                        const auto dataIndices = reinterpret_cast<const uint8_t *>(dataPtr);
                        for (size_t i = 0; i < accessorIndices.count; i++)
                            m_indices.push_back(static_cast<uint32_t>(dataIndices[i]));
                        break;
                    }
                    default:
                        PE_ERROR("Invalid indices type");
                        break;
                    }

                    indexOffset += static_cast<uint32_t>(accessorIndices.count);
                    progress += primitive.indices;
                }

                // -------- AABB Vertices --------
                primitiveInfo.aabbColor = (rand(0, 255) << 24) + (rand(0, 255) << 16) + (rand(0, 255) << 8) + 256;

                const vec3 &min = primitiveInfo.boundingBox.min;
                const vec3 &max = primitiveInfo.boundingBox.max;

                AabbVertex corners[8] = {
                    {min.x, min.y, min.z},
                    {max.x, min.y, min.z},
                    {max.x, max.y, min.z},
                    {min.x, max.y, min.z},
                    {min.x, min.y, max.z},
                    {max.x, min.y, max.z},
                    {max.x, max.y, max.z},
                    {min.x, max.y, max.z}};

                for (const auto &corner : corners)
                    m_aabbVertices.push_back(corner);
            }
        }
    }

    void ModelGltf::SetupNodes()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading_current;
        auto &total = gSettings.loading_total;
        auto &loading = gSettings.loading_name;

        progress = 0;
        total = static_cast<uint32_t>(nodes.size());
        loading = "Loading nodes";

        // update nodes info
        dirtyNodes = true; // set dirty so the 1st call to UpdateNodeMatrix will update nodeInfo.ubo.worldMatrix and primitiveInfo.worldBoundingBox
        m_nodesInfo.resize(nodes.size());
        for (int i = 0; i < nodes.size(); i++)
        {
            tinygltf::Node &node = nodes[i];
            NodeInfo &nodeInfo = m_nodesInfo[i];
            nodeInfo.dirty = true; // set dirty so the 1st call to UpdateNodeMatrix will update nodeInfo.ubo.worldMatrix and primitiveInfo.worldBoundingBox

            if (node.matrix.size() == 16)
            {
                nodeInfo.localMatrix = make_mat4(&node.matrix[0]);
            }
            else if (node.translation.size() == 3 || node.rotation.size() == 4 || node.scale.size() == 3)
            {
                auto &t = node.translation;
                auto &r = node.rotation;
                auto &s = node.scale;
                vec3 position = vec3(0.f);
                quat rotation = quat(1.f, 0.f, 0.f, 0.f);
                vec3 scale = vec3(1.f);

                if (node.translation.size() == 3)
                {
                    position[0] = static_cast<float>(t[0]);
                    position[1] = static_cast<float>(t[1]);
                    position[2] = static_cast<float>(t[2]);
                }
                if (node.rotation.size() == 4)
                {
                    rotation[0] = static_cast<float>(r[3]);
                    rotation[1] = static_cast<float>(r[0]);
                    rotation[2] = static_cast<float>(r[1]);
                    rotation[3] = static_cast<float>(r[2]);
                }
                if (node.scale.size() == 3)
                {
                    scale[0] = static_cast<float>(s[0]);
                    scale[1] = static_cast<float>(s[1]);
                    scale[2] = static_cast<float>(s[2]);
                }
                nodeInfo.localMatrix = translate(mat4(1.0f), position) * mat4(rotation) * glm::scale(mat4(1.0f), scale);
            }
            else
            {
                nodeInfo.localMatrix = mat4(1.0f);
            }

            for (int child : node.children)
            {
                m_nodesInfo[child].parent = i;
            }

            // Update loading bar
            progress++;
        }
    }

    void ModelGltf::UpdateAllNodeMatrices()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading_current;
        auto &total = gSettings.loading_total;
        auto &loading = gSettings.loading_name;

        progress = 0;
        total = static_cast<uint32_t>(nodes.size());
        loading = "Updating node matrices";
        for (int i = 0; i < nodes.size(); i++)
        {
            UpdateNodeMatrix(i);

            // Update loading bar
            progress++;
        }
    }

    void ModelGltf::UpdateNodeMatrices()
    {
        if (!dirtyNodes)
            return;

        tinygltf::Scene &scene = scenes[defaultScene];
        for (int i = 0; i < scene.nodes.size(); i++)
        {
            UpdateNodeMatrix(i);
        }

        dirtyNodes = false;
    }

    // Updates the node and recursively its children
    void ModelGltf::UpdateNodeMatrix(int node)
    {
        if (node < 0)
            return;

        NodeInfo &nodeInfo = m_nodesInfo[node];
        nodeInfo.ubo.previousWorldMatrix = nodeInfo.ubo.worldMatrix;
        if (nodeInfo.dirty)
        {
            mat4 trans = nodeInfo.localMatrix;
            int parent = nodeInfo.parent;
            while (parent >= 0)
            {
                const NodeInfo &parentInfo = m_nodesInfo[parent];
                trans = parentInfo.localMatrix * trans;
                parent = parentInfo.parent;
            }
            nodeInfo.ubo.worldMatrix = matrix * trans;
        }

        for (int child : nodes[node].children)
        {
            UpdateNodeMatrix(child);
        }

        if (nodeInfo.dirty)
        {
            int mesh = nodes[node].mesh;
            if (mesh < 0)
                return;

            auto &primitives = meshes[mesh].primitives;
            for (int i = 0; i < primitives.size(); i++)
            {
                // cache world bounding box
                PrimitiveInfo &primitiveInfo = m_meshesInfo[mesh].primitivesInfo[i];
                primitiveInfo.worldBoundingBox.min = nodeInfo.ubo.worldMatrix * vec4(primitiveInfo.boundingBox.min, 1.f);
                primitiveInfo.worldBoundingBox.max = nodeInfo.ubo.worldMatrix * vec4(primitiveInfo.boundingBox.max, 1.f);
            }

            nodeInfo.dirty = false;
            for (int frame = 0; frame < SWAPCHAIN_IMAGES; frame++)
            {
                nodeInfo.dirtyUniforms[frame] = true;
                dirtyUniforms[frame] = true;
            }
        }
    }

    void ModelGltf::SetPrimitiveFactors(Buffer *uniformBuffer)
    {
        // copy factors in uniform buffer
        BufferRange range{};
        uniformBuffer->Map();

        for (int i = 0; i < nodes.size(); i++)
        {
            int mesh = nodes[i].mesh;
            if (mesh < 0)
                continue;

            for (int j = 0; j < meshes[mesh].primitives.size(); j++)
            {
                tinygltf::Material &material = materials[meshes[mesh].primitives[j].material];

                mat4 factors;

                factors[0][0] = static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[0]);
                factors[0][1] = static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[1]);
                factors[0][2] = static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[2]);
                factors[0][3] = static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[3]);

                factors[1][0] = static_cast<float>(material.emissiveFactor[0]);
                factors[1][1] = static_cast<float>(material.emissiveFactor[1]);
                factors[1][2] = static_cast<float>(material.emissiveFactor[2]);

                factors[2][0] = static_cast<float>(material.pbrMetallicRoughness.metallicFactor);
                factors[2][1] = static_cast<float>(material.pbrMetallicRoughness.roughnessFactor);
                factors[2][2] = static_cast<float>(material.alphaCutoff);

                factors[3][0] = 0.f;                                                    // hasbones
                factors[3][1] = static_cast<float>(material.normalTexture.scale);       // scaledNormal = normalize((<sampled normal texture value> * 2.0 - 1.0) * vec3(<normal scale>, <normal scale>, 1.0))
                factors[3][2] = static_cast<float>(material.occlusionTexture.strength); // occludedColor = lerp(color, color * <sampled occlusion texture value>, <occlusion strength>)

                range.data = &factors;
                range.size = m_meshesInfo[mesh].primitivesInfo[j].dataSize;
                range.offset = m_meshesInfo[mesh].primitivesInfo[j].dataOffset;
                uniformBuffer->Copy(1, &range, true);
            }
        }

        uniformBuffer->Unmap();
    }

    void ModelGltf::UploadBuffers(CommandBuffer *cmd)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading_current;
        auto &total = gSettings.loading_total;
        auto &loading = gSettings.loading_name;

        progress = 0;
        total = 1;
        loading = "Upoading buffers";

        Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();
        scene.AddModel(this);
        scene.UpdateGeometryBuffers(cmd);

        progress++;
    }
}
