#include "Scene/ModelGltf.h"
#include "API/Queue.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Buffer.h"
#include "API/RHI.h"
#include "Systems/RendererSystem.h"
#include "Scene/Scene.h"

#undef max

namespace pe
{
    Image *g_defaultBlack = nullptr;
    Image *g_defaultNormal = nullptr;
    Image *g_defaultWhite = nullptr;
    Sampler *g_defaultSampler = nullptr;

    ModelGltf::ModelGltf() = default;

    ModelGltf *ModelGltf::Load(const std::filesystem::path &file)
    {
        PE_ERROR_IF(!std::filesystem::exists(file), std::string("Model file not found: " + file.string()).c_str());

        ModelGltf *modelGltf = new ModelGltf();
        ModelGltf &model = *modelGltf;

        auto &gSettings = Settings::Get<GlobalSettings>();
        gSettings.loading_name = "Reading from file";

        PE_ERROR_IF(!model.LoadFile(file), std::string("Failed to load model: " + file.string()).c_str());

        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();

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
        cmd->Return();

        modelGltf->m_render = true;

        return modelGltf;
    }

    bool ModelGltf::LoadFile(const std::filesystem::path &file)
    {
        tinygltf::TinyGLTF loader;
        std::string err, warn;

        if (file.extension() == ".gltf")
            return loader.LoadASCIIFromFile(&m_gltfModel, &err, &warn, file.string());
        else if (file.extension() == ".glb")
            return loader.LoadBinaryFromFile(&m_gltfModel, &err, &warn, file.string());
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
            vk::SamplerCreateInfo info = Sampler::CreateInfoInit();
            g_defaultSampler = Sampler::Create(info, "Default Sampler");
            m_samplers.push_back(g_defaultSampler);
        }

        m_images.reserve(m_gltfModel.images.size() + 3);
        for (auto &image : m_gltfModel.images)
        {
            PE_ERROR_IF(image.image.empty(), std::string("Image data is empty: " + image.uri).c_str());

            vk::ImageCreateInfo info = Image::CreateInfoInit();
            info.format = vk::Format::eR8G8B8A8Unorm;
            info.mipLevels = Image::CalculateMips(image.width, image.height);
            info.extent = vk::Extent3D{static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height), 1u};
            info.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage;
            info.initialLayout = vk::ImageLayout::ePreinitialized;

            Image *uploadImage = Image::Create(info, image.uri);
            uploadImage->CreateSRV(vk::ImageViewType::e2D);
            PE_ERROR_IF(!uploadImage->GetSRV(), std::string("Failed to create SRV for image: " + image.uri).c_str());

            void *data = image.image.data();
            size_t size = static_cast<uint32_t>(image.image.size());
            cmd->CopyDataToImageStaged(uploadImage, data, size);
            cmd->GenerateMipMaps(uploadImage);

            ImageBarrierInfo barrier{};
            barrier.image = uploadImage;
            barrier.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.stageFlags = vk::PipelineStageFlagBits2::eFragmentShader;
            barrier.accessMask = vk::AccessFlagBits2::eShaderRead;
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

        m_samplers.reserve(m_gltfModel.samplers.size() + 1);
        for (auto &sampler : m_gltfModel.samplers)
        {
            vk::SamplerCreateInfo info = Sampler::CreateInfoInit();
            if (sampler.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST)
            {
                info.minFilter = vk::Filter::eNearest;
            }
            else if (sampler.minFilter == TINYGLTF_TEXTURE_FILTER_LINEAR)
            {
                info.minFilter = vk::Filter::eLinear;
            }
            else if (sampler.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST)
            {
                info.minFilter = vk::Filter::eNearest;
                info.mipmapMode = vk::SamplerMipmapMode::eNearest;
            }
            else if (sampler.minFilter == TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST)
            {
                info.minFilter = vk::Filter::eLinear;
                info.mipmapMode = vk::SamplerMipmapMode::eNearest;
            }
            else if (sampler.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR)
            {
                info.minFilter = vk::Filter::eNearest;
                info.mipmapMode = vk::SamplerMipmapMode::eLinear;
            }
            else if (sampler.minFilter == TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR)
            {
                info.minFilter = vk::Filter::eLinear;
                info.mipmapMode = vk::SamplerMipmapMode::eLinear;
            }
            else
            {
                info.minFilter = vk::Filter::eLinear;
                info.mipmapMode = vk::SamplerMipmapMode::eLinear;
            }

            if (sampler.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST)
                info.magFilter = vk::Filter::eNearest;
            else
                info.magFilter = vk::Filter::eLinear;

            if (sampler.wrapS == TINYGLTF_TEXTURE_WRAP_REPEAT)
                info.addressModeU = vk::SamplerAddressMode::eRepeat;
            else if (sampler.wrapS == TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE)
                info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            else if (sampler.wrapS == TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT)
                info.addressModeU = vk::SamplerAddressMode::eMirroredRepeat;

            if (sampler.wrapT == TINYGLTF_TEXTURE_WRAP_REPEAT)
                info.addressModeV = vk::SamplerAddressMode::eRepeat;
            else if (sampler.wrapT == TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE)
                info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            else if (sampler.wrapT == TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT)
                info.addressModeV = vk::SamplerAddressMode::eMirroredRepeat;

            Sampler *uploadSampler = Sampler::Create(info, sampler.name);
            m_samplers.push_back(uploadSampler);

            // Update loading bar
            progress++;
        }
    }

    void ModelGltf::ExtractMaterialInfo()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading_current;
        auto &total = gSettings.loading_total;
        auto &loading = gSettings.loading_name;

        progress = 0;
        for (size_t i = 0; i < m_gltfModel.meshes.size(); i++)
        {
            tinygltf::Mesh &mesh = m_gltfModel.meshes[i];
            for (size_t j = 0; j < mesh.primitives.size(); j++)
                m_primitivesCount++;
        }
        total = m_primitivesCount;
        loading = "Loading material pipeline info";

        // Material info for pipeline creation
        m_meshesInfo.resize(m_gltfModel.meshes.size());
        for (size_t i = 0; i < m_gltfModel.meshes.size(); i++)
        {
            tinygltf::Mesh &mesh = m_gltfModel.meshes[i];
            MeshInfo &meshInfo = m_meshesInfo[i];
            meshInfo.primitivesInfo.resize(mesh.primitives.size());

            for (size_t j = 0; j < mesh.primitives.size(); j++)
            {
                tinygltf::Primitive &primitive = mesh.primitives[j];
                PrimitiveInfo &primitiveInfo = meshInfo.primitivesInfo[j];

                tinygltf::Material &material = m_gltfModel.materials[primitive.material];
                MaterialInfo &materialInfo = primitiveInfo.materialInfo;

                switch (primitive.mode)
                {
                case TINYGLTF_MODE_POINTS:
                    materialInfo.topology = vk::PrimitiveTopology::ePointList;
                    break;
                case TINYGLTF_MODE_LINE:
                    materialInfo.topology = vk::PrimitiveTopology::eLineList;
                    break;
                case TINYGLTF_MODE_LINE_STRIP:
                    materialInfo.topology = vk::PrimitiveTopology::eLineStrip;
                    break;
                case TINYGLTF_MODE_TRIANGLES:
                    materialInfo.topology = vk::PrimitiveTopology::eTriangleList;
                    break;
                case TINYGLTF_MODE_TRIANGLE_STRIP:
                    materialInfo.topology = vk::PrimitiveTopology::eTriangleStrip;
                    break;
                case TINYGLTF_MODE_TRIANGLE_FAN:
                    materialInfo.topology = vk::PrimitiveTopology::eTriangleFan;
                    break;
                default:
                    PE_ERROR("Invalid primitive mode");
                    break;
                }
                materialInfo.cullMode = material.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eFront;
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
        for (int i = 0; i < m_gltfModel.meshes.size(); i++)
        {
            tinygltf::Mesh &mesh = m_gltfModel.meshes[i];
            MeshInfo &meshInfo = m_meshesInfo[i];
            for (int j = 0; j < mesh.primitives.size(); j++)
            {
                auto posIt = mesh.primitives[j].attributes.find("POSITION");
                const auto &accessorPos = m_gltfModel.accessors[posIt->second];
                const auto &accessorIndices = m_gltfModel.accessors[mesh.primitives[j].indices];

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

        for (uint32_t i = 0; i < m_gltfModel.meshes.size(); i++)
        {
            tinygltf::Mesh &mesh = m_gltfModel.meshes[i];
            MeshInfo &meshInfo = m_meshesInfo[i];

            for (uint32_t j = 0; j < mesh.primitives.size(); j++)
            {
                tinygltf::Primitive &primitive = mesh.primitives[j];
                PrimitiveInfo &primitiveInfo = meshInfo.primitivesInfo[j];
                tinygltf::Material &material = m_gltfModel.materials[primitive.material];

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
                        const auto &texture = m_gltfModel.textures[gltfIndex];
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
                auto getAttributeData = [this, &primitive](const char* attrName) -> std::pair<const uint8_t*, const tinygltf::Accessor*>
                {
                    auto attrIt = primitive.attributes.find(attrName);
                    if (attrIt == primitive.attributes.end())
                        return {nullptr, nullptr};

                    const auto &accessor = m_gltfModel.accessors[attrIt->second];
                    if (accessor.bufferView < 0)
                        return {nullptr, &accessor};

                    const auto &bufferView = m_gltfModel.bufferViews[accessor.bufferView];
                    if (bufferView.buffer < 0)
                        return {nullptr, &accessor};

                    const auto &buffer = m_gltfModel.buffers[bufferView.buffer];
                    if (buffer.data.empty())
                        return {nullptr, &accessor};

                    size_t offset = accessor.byteOffset + bufferView.byteOffset;
                    if (offset >= buffer.data.size())
                        return {nullptr, &accessor};

                    return {&buffer.data[offset], &accessor};
                };

                auto [dataPosRaw, accessorPos] = getAttributeData("POSITION");
                auto [dataUV0Raw, accessorUV0] = getAttributeData("TEXCOORD_0");
                auto [dataNormRaw, accessorNorm] = getAttributeData("NORMAL");
                auto [dataColRaw, accessorCol] = getAttributeData("COLOR_0");
                auto [dataJointsRaw, accessorJoints] = getAttributeData("JOINTS_0");
                auto [dataWeightsRaw, accessorWeights] = getAttributeData("WEIGHTS_0");

                const float* dataPos = reinterpret_cast<const float*>(dataPosRaw);
                const float* dataUV0 = reinterpret_cast<const float*>(dataUV0Raw);
                const float* dataNormal = reinterpret_cast<const float*>(dataNormRaw);
                const float* dataColor = reinterpret_cast<const float*>(dataColRaw);
                const void* dataJoints = dataJointsRaw;
                const float* dataWeights = reinterpret_cast<const float*>(dataWeightsRaw);
                PE_ERROR_IF(!accessorPos, "No POSITION attribute found in primitive");
                PE_ERROR_IF(!dataPos, "POSITION attribute stream is empty");

                for (uint32_t i = 0; i < accessorPos->count; i++)
                {
                    Vertex vertex{};
                    PositionUvVertex positionUvVertex{};

                    // Position
                    FillVertexPosition(vertex, dataPos[i * 3], dataPos[i * 3 + 1], dataPos[i * 3 + 2]);
                    FillVertexPosition(positionUvVertex, dataPos[i * 3], dataPos[i * 3 + 1], dataPos[i * 3 + 2]);

                    // UV
                    if (dataUV0)
                        FillVertexUV(vertex, dataUV0[i * 2], dataUV0[i * 2 + 1]);
                    else
                        FillVertexUV(vertex, 0.0f, 0.0f);

                    // Normal
                    if (dataNormal)
                        FillVertexNormal(vertex, dataNormal[i * 3], dataNormal[i * 3 + 1], dataNormal[i * 3 + 2]);
                    else
                        FillVertexNormal(vertex, 0.0f, 0.0f, 1.0f);

                    // Color
                    if (dataColor)
                        FillVertexColor(vertex, dataColor[i * 4], dataColor[i * 4 + 1], dataColor[i * 4 + 2], dataColor[i * 4 + 3]);
                    else
                        FillVertexColor(vertex, 1.0f, 1.0f, 1.0f, 1.0f);

                    // Joints and Weights
                    uint8_t joints[4] = {0, 0, 0, 0};
                    float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};

                    if (dataJoints && accessorJoints)
                    {
                        const size_t jointIdx = i * 4;
                        if (accessorJoints->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                        {
                            const uint8_t *dataJoints8 = reinterpret_cast<const uint8_t *>(dataJoints);
                            joints[0] = dataJoints8[jointIdx];
                            joints[1] = dataJoints8[jointIdx + 1];
                            joints[2] = dataJoints8[jointIdx + 2];
                            joints[3] = dataJoints8[jointIdx + 3];
                        }
                        else if (accessorJoints->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                        {
                            const uint16_t *dataJoints16 = reinterpret_cast<const uint16_t *>(dataJoints);
                            joints[0] = static_cast<uint8_t>(dataJoints16[jointIdx]);
                            joints[1] = static_cast<uint8_t>(dataJoints16[jointIdx + 1]);
                            joints[2] = static_cast<uint8_t>(dataJoints16[jointIdx + 2]);
                            joints[3] = static_cast<uint8_t>(dataJoints16[jointIdx + 3]);
                        }
                    }

                    if (dataWeights)
                    {
                        const size_t weightIdx = i * 4;
                        weights[0] = dataWeights[weightIdx];
                        weights[1] = dataWeights[weightIdx + 1];
                        weights[2] = dataWeights[weightIdx + 2];
                        weights[3] = dataWeights[weightIdx + 3];
                    }

                    FillVertexJointsWeights(vertex, joints[0], joints[1], joints[2], joints[3],
                                          weights[0], weights[1], weights[2], weights[3]);
                    FillVertexJointsWeights(positionUvVertex, joints[0], joints[1], joints[2], joints[3],
                                          weights[0], weights[1], weights[2], weights[3]);

                    m_vertices.push_back(vertex);
                    m_positionUvs.push_back(positionUvVertex);
                    progress++;
                }

                // -------- Indices --------
                if (primitive.indices >= 0)
                {
                    const auto &accessorIndices = m_gltfModel.accessors[primitive.indices];
                    const auto &bufferViewIndices = m_gltfModel.bufferViews[accessorIndices.bufferView];
                    const auto &bufferIndices = m_gltfModel.buffers[bufferViewIndices.buffer];
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
        total = static_cast<uint32_t>(m_gltfModel.nodes.size());
        loading = "Loading nodes";

        // update nodes info
        dirtyNodes = true; // set dirty so the 1st call to UpdateNodeMatrix will update nodeInfo.ubo.worldMatrix and primitiveInfo.worldBoundingBox
        m_nodesInfo.resize(m_gltfModel.nodes.size());
        for (int i = 0; i < m_gltfModel.nodes.size(); i++)
        {
            tinygltf::Node &node = m_gltfModel.nodes[i];
            NodeInfo &nodeInfo = m_nodesInfo[i];
            nodeInfo.dirty = true; // set dirty so the 1st call to UpdateNodeMatrix will update nodeInfo.ubo.worldMatrix and primitiveInfo.worldBoundingBox
            nodeInfo.dirtyUniforms.resize(RHII.GetSwapchainImageCount(), false);
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
        total = static_cast<uint32_t>(m_gltfModel.nodes.size());
        loading = "Updating node matrices";
        for (int i = 0; i < m_gltfModel.nodes.size(); i++)
        {
            UpdateNodeMatrix(i);

            // Update loading bar
            progress++;
        }
    }

    // Updates the node and recursively its children
    void ModelGltf::UpdateNodeMatrix(int node)
    {
        Model::UpdateNodeMatrix(node);

        // Update children recursively
        if (node >= 0 && node < static_cast<int>(m_gltfModel.nodes.size()))
        {
            for (int child : m_gltfModel.nodes[node].children)
            {
                UpdateNodeMatrix(child);
            }
        }
    }

    int ModelGltf::GetMeshCount() const
    {
        return static_cast<int>(m_gltfModel.meshes.size());
    }

    int ModelGltf::GetNodeCount() const
    {
        return static_cast<int>(m_gltfModel.nodes.size());
    }

    int ModelGltf::GetNodeMesh(int nodeIndex) const
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_gltfModel.nodes.size()))
            return -1;
        return m_gltfModel.nodes[nodeIndex].mesh;
    }

    int ModelGltf::GetMeshPrimitiveCount(int meshIndex) const
    {
        if (meshIndex < 0 || meshIndex >= static_cast<int>(m_gltfModel.meshes.size()))
            return 0;
        return static_cast<int>(m_gltfModel.meshes[meshIndex].primitives.size());
    }

    int ModelGltf::GetPrimitiveMaterial(int meshIndex, int primitiveIndex) const
    {
        if (meshIndex < 0 || meshIndex >= static_cast<int>(m_gltfModel.meshes.size()))
            return -1;
        if (primitiveIndex < 0 || primitiveIndex >= static_cast<int>(m_gltfModel.meshes[meshIndex].primitives.size()))
            return -1;
        return m_gltfModel.meshes[meshIndex].primitives[primitiveIndex].material;
    }

    void ModelGltf::GetPrimitiveMaterialFactors(int meshIndex, int primitiveIndex, mat4 &factors) const
    {
        int materialIndex = GetPrimitiveMaterial(meshIndex, primitiveIndex);
        if (materialIndex < 0 || materialIndex >= static_cast<int>(m_gltfModel.materials.size()))
        {
            factors = mat4(1.f);
            return;
        }

        const tinygltf::Material &material = m_gltfModel.materials[materialIndex];

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
        factors[3][1] = static_cast<float>(material.normalTexture.scale);       // scaledNormal
        factors[3][2] = static_cast<float>(material.occlusionTexture.strength); // occlusion strength
    }

    float ModelGltf::GetPrimitiveAlphaCutoff(int meshIndex, int primitiveIndex) const
    {
        int materialIndex = GetPrimitiveMaterial(meshIndex, primitiveIndex);
        if (materialIndex < 0 || materialIndex >= static_cast<int>(m_gltfModel.materials.size()))
            return 0.5f;
        return static_cast<float>(m_gltfModel.materials[materialIndex].alphaCutoff);
    }
}
