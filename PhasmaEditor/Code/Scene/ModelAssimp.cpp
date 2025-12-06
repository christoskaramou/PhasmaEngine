#include "Scene/ModelAssimp.h"
#include "API/Queue.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Buffer.h"
#include "API/RHI.h"
#include "API/Image.h"
#include "Systems/RendererSystem.h"
#include "Scene/Scene.h"
#include "Base/Path.h"
#include <assimp/GltfMaterial.h>

#undef max

namespace pe
{
    Image *g_defaultBlack;
    Image *g_defaultNormal;
    Image *g_defaultWhite;
    Sampler *g_defaultSampler;

    ModelAssimp::ModelAssimp() = default;

    ModelAssimp *ModelAssimp::Load(const std::filesystem::path &file)
    {
        PE_ERROR_IF(!std::filesystem::exists(file), std::string("Model file not found: " + file.string()).c_str());

        ModelAssimp *modelAssimp = new ModelAssimp();
        ModelAssimp &model = *modelAssimp;
        model.SetLabel(file.filename().string());

        auto &gSettings = Settings::Get<GlobalSettings>();
        gSettings.loading_name = "Reading from file";

        PE_ERROR_IF(!model.LoadFile(file), std::string("Failed to load model: " + file.string()).c_str());

        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();

        cmd->Begin();
        model.UploadImages(cmd);
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

        modelAssimp->m_render = true;

        return modelAssimp;
    }

    bool ModelAssimp::LoadFile(const std::filesystem::path &file)
    {
        m_filePath = file.parent_path();

        uint32_t flags = 0;
        flags |= aiProcess_ValidateDataStructure;
        flags |= aiProcess_Triangulate;
        flags |= aiProcess_SortByPType;
        flags |= aiProcess_CalcTangentSpace;
        flags |= aiProcess_GenSmoothNormals;
        flags |= aiProcess_GenUVCoords;
        flags |= aiProcess_RemoveRedundantMaterials;
        flags |= aiProcess_FindDegenerates;
        flags |= aiProcess_FindInvalidData;
        // flags |= aiProcess_PreTransformVertices;
        // flags |= aiProcess_OptimizeMeshes;
        // flags |= aiProcess_FindInstances;
        flags |= aiProcess_MakeLeftHanded;
        flags |= aiProcess_FlipUVs;
        flags |= aiProcess_FlipWindingOrder;

        m_scene = m_importer.ReadFile(file.string(), flags);
        if (!m_scene || m_scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !m_scene->mRootNode)
        {
            PE_ERROR(std::string("Assimp error: " + std::string(m_importer.GetErrorString())).c_str());
            return false;
        }

        return true;
    }

    void ModelAssimp::UploadImages(CommandBuffer *cmd)
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

        const std::initializer_list<aiTextureType> textureTypes = {
            aiTextureType_BASE_COLOR,
            aiTextureType_DIFFUSE,
            aiTextureType_NORMALS,
            aiTextureType_UNKNOWN,
            aiTextureType_SPECULAR,
            aiTextureType_LIGHTMAP,
            aiTextureType_EMISSIVE,
            aiTextureType_METALNESS,
            aiTextureType_DIFFUSE_ROUGHNESS};

        m_images.clear();
        m_textureCache.clear();

        // Count total textures for progress UI
        total = 0;
        for (unsigned int i = 0; i < m_scene->mNumMaterials; i++)
        {
            aiMaterial *material = m_scene->mMaterials[i];
            for (aiTextureType type : textureTypes)
                total += material->GetTextureCount(type);
        }

        progress = 0;
        loading = "Loading textures";

        m_images.reserve(total + 3);

        for (unsigned int i = 0; i < m_scene->mNumMaterials; i++)
        {
            aiMaterial *material = m_scene->mMaterials[i];
            for (aiTextureType type : textureTypes)
            {
                unsigned int textureCount = material->GetTextureCount(type);
                for (unsigned int j = 0; j < textureCount; j++)
                {
                    std::filesystem::path texPath = GetTexturePath(material, type, j);
                    if (texPath.empty())
                        continue;

                    bool needsLoad = FindTexture(texPath) == nullptr;
                    Image *img = LoadTexture(cmd, texPath);
                    if (img && needsLoad)
                        progress++;
                }
            }
        }

        auto ensureDefaultImage = [&](Image *image)
        {
            if (!image)
                return;
            if (std::find(m_images.begin(), m_images.end(), image) == m_images.end())
                m_images.push_back(image);
        };
        ensureDefaultImage(g_defaultBlack);
        ensureDefaultImage(g_defaultNormal);
        ensureDefaultImage(g_defaultWhite);
    }

    void ModelAssimp::ExtractMaterialInfo()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading_current;
        auto &total = gSettings.loading_total;
        auto &loading = gSettings.loading_name;

        progress = 0;
        m_meshCount = 0;

        for (unsigned int i = 0; i < m_scene->mNumMeshes; i++)
        {
            const aiMesh *mesh = m_scene->mMeshes[i];
            if (mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)
                m_meshCount++;
        }

        total = m_meshCount;
        loading = "Loading material pipeline info";

        m_meshInfos.resize(m_scene->mNumMeshes);

        for (unsigned int i = 0; i < m_scene->mNumMeshes; i++)
        {
            const aiMesh *mesh = m_scene->mMeshes[i];
            MeshInfo &meshInfo = m_meshInfos[i];
            MaterialInfo &materialInfo = meshInfo.materialInfo;

            materialInfo.topology = vk::PrimitiveTopology::eTriangleList;

            int twoSided = 0;
            aiMaterial *material = m_scene->mMaterials[mesh->mMaterialIndex];
            material->Get(AI_MATKEY_TWOSIDED, twoSided);
            materialInfo.cullMode = twoSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eFront;
            RenderType renderType = DetermineRenderType(material);
            materialInfo.alphaBlend = renderType != RenderType::Opaque;

            progress++;
        }
    }

    void ModelAssimp::ProcessAabbs()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading_current;
        auto &total = gSettings.loading_total;
        auto &loading = gSettings.loading_name;

        progress = 0;
        total = m_meshCount;
        loading = "Loading aabbs";

        m_meshCount = 0;
        m_verticesCount = 0;
        m_indicesCount = 0;

        for (unsigned int i = 0; i < m_scene->mNumMeshes; i++)
        {
            const aiMesh *mesh = m_scene->mMeshes[i];
            if (!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE))
                continue;

            MeshInfo &meshInfo = m_meshInfos[i];

            meshInfo.vertexOffset = m_verticesCount;
            meshInfo.verticesCount = mesh->mNumVertices;
            meshInfo.indexOffset = m_indicesCount;
            meshInfo.indicesCount = mesh->mNumFaces * 3; // Triangles
            meshInfo.aabbVertexOffset = m_meshCount * 8;

            // Calculate bounding box
            if (mesh->mNumVertices > 0)
            {
                meshInfo.boundingBox.min = vec3(mesh->mVertices[0].x, mesh->mVertices[0].y, mesh->mVertices[0].z);
                meshInfo.boundingBox.max = meshInfo.boundingBox.min;

                for (unsigned int j = 1; j < mesh->mNumVertices; j++)
                {
                    vec3 pos(mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z);
                    meshInfo.boundingBox.min = glm::min(meshInfo.boundingBox.min, pos);
                    meshInfo.boundingBox.max = glm::max(meshInfo.boundingBox.max, pos);
                }
            }

            m_verticesCount += mesh->mNumVertices;
            m_indicesCount += mesh->mNumFaces * 3;
            m_meshCount++;

            progress++;
        }
    }

    void ModelAssimp::AcquireGeometryInfo()
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
        m_aabbVertices.reserve(m_meshCount * 8);
        m_indices.reserve(m_indicesCount);

        uint32_t indexBaseOffset = 0; // Track index offset to ensure it matches ProcessAabbs

        for (unsigned int i = 0; i < m_scene->mNumMeshes; i++)
        {
            const aiMesh *mesh = m_scene->mMeshes[i];
            if (!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE))
                continue;

            MeshInfo &meshInfo = m_meshInfos[i];
            aiMaterial *material = m_scene->mMaterials[mesh->mMaterialIndex];

            meshInfo.renderType = DetermineRenderType(material);

            AssignTexture(meshInfo, TextureType::BaseColor, material, {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE}, g_defaultBlack);
            AssignTexture(meshInfo, TextureType::MetallicRoughness, material, {aiTextureType_UNKNOWN, aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_SPECULAR, aiTextureType_METALNESS}, g_defaultBlack);
            AssignTexture(meshInfo, TextureType::Normal, material, {aiTextureType_NORMAL_CAMERA, aiTextureType_NORMALS, aiTextureType_HEIGHT}, g_defaultNormal);
            AssignTexture(meshInfo, TextureType::Occlusion, material, {aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP}, g_defaultWhite);
            AssignTexture(meshInfo, TextureType::Emissive, material, {aiTextureType_EMISSION_COLOR, aiTextureType_EMISSIVE}, g_defaultBlack);
            ComputeMaterialData(meshInfo, material);

            // Process vertices - cache pointers for better performance
            const aiVector3D *positions = mesh->mVertices;
            const aiVector3D *normals = mesh->mNormals;
            const aiVector3D *uvs = mesh->mTextureCoords[0];
            const aiColor4D *colors = mesh->mColors[0];

            for (unsigned int j = 0; j < mesh->mNumVertices; j++)
            {
                Vertex vertex{};
                PositionUvVertex positionUvVertex{};

                // Position
                const aiVector3D &pos = positions[j];
                FillVertexPosition(vertex, pos.x, pos.y, pos.z);
                FillVertexPosition(positionUvVertex, pos.x, pos.y, pos.z);

                // UV
                if (uvs)
                {
                    FillVertexUV(vertex, uvs[j].x, uvs[j].y);
                    FillVertexUV(positionUvVertex, uvs[j].x, uvs[j].y);
                }
                else
                {
                    FillVertexUV(vertex, 0.0f, 0.0f);
                    FillVertexUV(positionUvVertex, 0.0f, 0.0f);
                }

                // Normal
                if (normals)
                {
                    const aiVector3D &norm = normals[j];
                    FillVertexNormal(vertex, norm.x, norm.y, norm.z);
                }
                else
                    FillVertexNormal(vertex, 0.0f, 0.0f, 1.0f);

                // Color
                if (colors)
                {
                    const aiColor4D &col = colors[j];
                    FillVertexColor(vertex, col.r, col.g, col.b, col.a);
                }
                else
                    FillVertexColor(vertex, 1.0f, 1.0f, 1.0f, 1.0f);

                // Joints and weights (for skeletal animation - not implemented yet)
                FillVertexJointsWeights(vertex, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f);
                FillVertexJointsWeights(positionUvVertex, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f);

                m_vertices.push_back(vertex);
                m_positionUvs.push_back(positionUvVertex);
                progress++;
            }

            // Process indices
            // Verify indexOffset matches what we set in ProcessAabbs
            PE_ERROR_IF(meshInfo.indexOffset != indexBaseOffset, "Index offset mismatch in AcquireGeometryInfo");

            const size_t expectedIndices = mesh->mNumFaces * 3;
            const size_t indicesStart = m_indices.size();
            m_indices.resize(indicesStart + expectedIndices);

            size_t indexWrite = indicesStart;
            for (unsigned int j = 0; j < mesh->mNumFaces; j++)
            {
                const aiFace &face = mesh->mFaces[j];
                if (face.mNumIndices == 3)
                {
                    m_indices[indexWrite++] = face.mIndices[0];
                    m_indices[indexWrite++] = face.mIndices[1];
                    m_indices[indexWrite++] = face.mIndices[2];
                }
            }
            m_indices.resize(indexWrite); // Trim if some faces weren't triangles
            progress += mesh->mNumFaces;

            // Update offsets for next mesh
            indexBaseOffset += mesh->mNumFaces * 3;

            // AABB vertices
            meshInfo.aabbColor = (rand(0, 255) << 24) + (rand(0, 255) << 16) + (rand(0, 255) << 8) + 256;

            const vec3 &min = meshInfo.boundingBox.min;
            const vec3 &max = meshInfo.boundingBox.max;

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

    void CountNodes(const aiNode *node, int &count)
    {
        count++;
        for (unsigned int i = 0; i < node->mNumChildren; i++)
        {
            CountNodes(node->mChildren[i], count);
        }
    }

    void ModelAssimp::SetupNodes()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading_current;
        auto &total = gSettings.loading_total;
        auto &loading = gSettings.loading_name;

        progress = 0;
        loading = "Loading nodes";

        // Count total nodes first
        int nodeCount = 0;
        CountNodes(m_scene->mRootNode, nodeCount);
        total = nodeCount;

        dirtyNodes = true;
        m_nodeToMesh.clear();
        m_nodeInfos.clear();
        m_nodeInfos.reserve(nodeCount);

        ProcessNode(m_scene->mRootNode, -1);

        m_nodeToMesh.resize(GetNodeCount(), -1);
        progress = GetNodeCount();
    }

    void ModelAssimp::ProcessNode(const aiNode *node, int parentIndex)
    {
        auto pushNode = [&](int parent, const mat4 &local, int meshIdx) -> int
        {
            int idx = static_cast<int>(m_nodeInfos.size());
            m_nodeInfos.push_back(NodeInfo{});
            NodeInfo &ni = m_nodeInfos.back();
            ni.name = node && node->mName.length > 0 ? node->mName.C_Str() : ("Node " + std::to_string(idx));

            ni.parent = parent;
            ni.localMatrix = local;
            ni.dirty = true;
            ni.dirtyUniforms.resize(RHII.GetSwapchainImageCount(), false);

            if (m_nodeToMesh.size() <= static_cast<size_t>(idx))
                m_nodeToMesh.resize(idx + 1, -1);
            m_nodeToMesh[idx] = meshIdx;

            return idx;
        };

        // Assimp -> GLM (transpose)
        aiMatrix4x4 aiMat = node->mTransformation;
        mat4 local(
            aiMat.a1, aiMat.b1, aiMat.c1, aiMat.d1,
            aiMat.a2, aiMat.b2, aiMat.c2, aiMat.d2,
            aiMat.a3, aiMat.b3, aiMat.c3, aiMat.d3,
            aiMat.a4, aiMat.b4, aiMat.c4, aiMat.d4);

        // Transform node that owns children
        int transformNodeIdx = pushNode(parentIndex, local, -1);

        // Attach meshes (one per node, glTF-like)
        if (node->mNumMeshes > 0)
        {
            bool firstAssigned = false;

            for (unsigned int k = 0; k < node->mNumMeshes; ++k)
            {
                int meshIdx = node->mMeshes[k];
                if (meshIdx < 0 || meshIdx >= static_cast<int>(m_scene->mNumMeshes))
                    continue;

                const aiMesh *m = m_scene->mMeshes[meshIdx];
                if (!(m->mPrimitiveTypes & aiPrimitiveType_TRIANGLE))
                    continue; // ignore non-triangle splits

                if (!firstAssigned)
                {
                    m_nodeToMesh[transformNodeIdx] = meshIdx;
                    firstAssigned = true;
                }
                else
                {
                    // sibling with same transform
                    pushNode(parentIndex, local, meshIdx);
                }
            }
        }

        // Children inherit transformNodeIdx
        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            ProcessNode(node->mChildren[i], transformNodeIdx);
    }

    void ModelAssimp::UpdateAllNodeMatrices()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading_current;
        auto &total = gSettings.loading_total;
        auto &loading = gSettings.loading_name;

        progress = 0;
        // Count root nodes (nodes with parent == -1)
        int rootNodeCount = 0;
        for (int i = 0; i < GetNodeCount(); i++)
        {
            if (m_nodeInfos[i].parent == -1)
                rootNodeCount++;
        }
        total = rootNodeCount;
        loading = "Updating node matrices";

        // Update root nodes only - recursive UpdateNodeMatrix will handle children
        for (int i = 0; i < GetNodeCount(); i++)
        {
            if (m_nodeInfos[i].parent == -1)
            {
                UpdateNodeMatrix(i);
                progress++;
            }
        }
    }

    void ModelAssimp::UpdateNodeMatrix(int node)
    {
        // Call base class to handle the matrix update
        Model::UpdateNodeMatrix(node);

        for (int i = 0; i < GetNodeCount(); i++)
        {
            if (m_nodeInfos[i].parent == node)
                UpdateNodeMatrix(i);
        }
    }

    int ModelAssimp::GetNodeCount() const
    {
        return static_cast<int>(m_nodeInfos.size());
    }

    int ModelAssimp::GetNodeMesh(int nodeIndex) const
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeToMesh.size()))
            return -1;
        return m_nodeToMesh[nodeIndex];
    }

    std::filesystem::path ModelAssimp::GetTexturePath(const aiMaterial *material, aiTextureType type, int index) const
    {
        aiString path;
        if (material->GetTexture(type, index, &path) != AI_SUCCESS || path.length == 0)
            return {};

        if (path.C_Str()[0] == '*')
            return {};

        return ResolveTexturePath(path.C_Str());
    }

    std::filesystem::path ModelAssimp::ResolveTexturePath(const std::filesystem::path &relativePath) const
    {
        std::filesystem::path candidates[] = {
            relativePath,
            m_filePath / relativePath,
            m_filePath / relativePath.filename()};

        for (const auto &candidate : candidates)
        {
            if (candidate.empty())
                continue;

            std::error_code ec;
            auto normalized = candidate.is_absolute() ? std::filesystem::weakly_canonical(candidate, ec) : (m_filePath / candidate).lexically_normal();
            if (!ec && std::filesystem::exists(normalized))
                return normalized;
            if (std::filesystem::exists(candidate))
                return candidate;
        }

        return {};
    }

    Image *ModelAssimp::LoadTexture(CommandBuffer *cmd, const std::filesystem::path &texturePath)
    {
        if (texturePath.empty())
            return nullptr;

        std::error_code ec;
        std::filesystem::path normalized = std::filesystem::weakly_canonical(texturePath, ec);
        if (ec)
            normalized = texturePath;

        std::string key = normalized.generic_string();
        auto it = m_textureCache.find(key);
        if (it != m_textureCache.end())
            return it->second;

        if (!std::filesystem::exists(normalized))
            return nullptr;

        Image *image = Image::LoadRGBA8(cmd, normalized.string());
        if (!image)
            return nullptr;

        m_images.push_back(image);
        m_textureCache.emplace(std::move(key), image);
        return image;
    }

    Image *ModelAssimp::FindTexture(const std::filesystem::path &texturePath) const
    {
        if (texturePath.empty())
            return nullptr;

        auto it = m_textureCache.find(texturePath.generic_string());
        if (it == m_textureCache.end())
            return nullptr;
        return it->second;
    }

    void ModelAssimp::AssignTexture(MeshInfo &meshInfo, TextureType type, aiMaterial *material, std::initializer_list<aiTextureType> textureTypes, Image *defaultImage)
    {
        Image *image = defaultImage;
        bool hasTexture = false;
        for (aiTextureType aiType : textureTypes)
        {
            std::filesystem::path texPath = GetTexturePath(material, aiType, 0);
            if (texPath.empty())
                continue;

            if (Image *loaded = FindTexture(texPath))
            {
                image = loaded;
                hasTexture = true;
                break;
            }
        }

        meshInfo.images[static_cast<int>(type)] = image ? image : defaultImage;
        meshInfo.samplers[static_cast<int>(type)] = g_defaultSampler;
        if (hasTexture)
            meshInfo.textureMask |= (1u << static_cast<uint32_t>(type));
    }

    void ModelAssimp::ComputeMaterialData(MeshInfo &meshInfo, aiMaterial *material) const
    {
        mat4 factors(1.f);
        float alphaCutoff = 0.5f;

        if (!material)
        {
            meshInfo.materialFactors = factors;
            meshInfo.alphaCutoff = alphaCutoff;
            return;
        }

        const bool hasNormalMap = (meshInfo.textureMask & TextureBit(TextureType::Normal)) != 0;
        const bool hasOcclusionMap = (meshInfo.textureMask & TextureBit(TextureType::Occlusion)) != 0;

        aiColor4D baseColor(1.f, 1.f, 1.f, 1.f);
        aiColor3D diffuseColor(1.f, 1.f, 1.f);
        aiColor3D emissive(0.f, 0.f, 0.f);
        aiColor3D specular(0.f, 0.f, 0.f);
        float metallic = 0.f;
        float roughness = 1.f;
        float normalScale = hasNormalMap ? 1.f : 0.f;
        float occlusionStrength = hasOcclusionMap ? 1.f : 0.f;

#ifdef AI_MATKEY_BASE_COLOR
        if (material->Get(AI_MATKEY_BASE_COLOR, baseColor) != AI_SUCCESS)
#endif
        {
            if (material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor) == AI_SUCCESS)
            {
                baseColor.r = diffuseColor.r;
                baseColor.g = diffuseColor.g;
                baseColor.b = diffuseColor.b;
                baseColor.a = 1.f;
            }
        }

        material->Get(AI_MATKEY_COLOR_EMISSIVE, emissive);
        material->Get(AI_MATKEY_COLOR_SPECULAR, specular);

#ifdef AI_MATKEY_METALLIC_FACTOR
        bool hasMetallicFactor = material->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS;
#else
        bool hasMetallicFactor = false;
#endif
#ifdef AI_MATKEY_ROUGHNESS_FACTOR
        bool hasRoughnessFactor = material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS;
#else
        bool hasRoughnessFactor = false;
#endif
#ifdef AI_MATKEY_GLTF_ALPHACUTOFF
        material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, alphaCutoff);
#endif

        if (!hasMetallicFactor)
        {
            float specularIntensity = std::max(specular.r, std::max(specular.g, specular.b));
            metallic = glm::clamp(specularIntensity, 0.f, 1.f);
        }

        if (!hasRoughnessFactor)
        {
            float shininess = 0.f;
            if (material->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS && shininess > 0.f)
                roughness = glm::clamp(std::sqrt(2.f / (shininess + 2.f)), 0.f, 1.f);
        }

        factors[0][0] = baseColor.r;
        factors[0][1] = baseColor.g;
        factors[0][2] = baseColor.b;
        factors[0][3] = baseColor.a;

        factors[1][0] = emissive.r;
        factors[1][1] = emissive.g;
        factors[1][2] = emissive.b;

        factors[2][0] = metallic;
        factors[2][1] = roughness;
        factors[2][2] = alphaCutoff;
        factors[2][3] = occlusionStrength;

        factors[3][0] = 0.f;
        factors[3][1] = normalScale;
        factors[3][2] = 0.f;

        meshInfo.materialFactors = factors;
        meshInfo.alphaCutoff = alphaCutoff;
    }

    RenderType ModelAssimp::DetermineRenderType(aiMaterial *material) const
    {
        aiString alphaMode;
        if (material->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS)
        {
            std::string mode(alphaMode.C_Str());
            std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c)
                           { return static_cast<char>(std::toupper(c)); });

            if (mode == "BLEND")
                return RenderType::AlphaBlend;
            if (mode == "MASK")
                return RenderType::AlphaCut;
        }

        float opacity = 1.0f;
        if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS && opacity < 1.0f)
            return RenderType::AlphaBlend;

        return RenderType::Opaque;
    }
}
