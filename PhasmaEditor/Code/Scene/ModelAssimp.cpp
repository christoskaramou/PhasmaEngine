#include "Scene/ModelAssimp.h"
#include "API/Command.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Systems/RendererSystem.h"
#include <assimp/GltfMaterial.h>
#include <assimp/ProgressHandler.hpp>

#undef max

namespace pe
{
    namespace
    {
        static void CountNodesRecursive(const aiNode *node, int &count)
        {
            count++;
            for (unsigned int i = 0; i < node->mNumChildren; i++)
                CountNodesRecursive(node->mChildren[i], count);
        }

        static inline mat4 AiToGlmMat4(const aiMatrix4x4 &a)
        {
            // Assimp is row-major-ish; your old code did the "transpose-style" mapping already.
            return mat4(
                a.a1, a.b1, a.c1, a.d1,
                a.a2, a.b2, a.c2, a.d2,
                a.a3, a.b3, a.c3, a.d3,
                a.a4, a.b4, a.c4, a.d4);
        }

        class CustomAssimpProgressHandler : public Assimp::ProgressHandler
        {
        public:
            bool Update(float percentage) override
            {
                auto &gSettings = Settings::Get<GlobalSettings>();
                gSettings.loading_total = 100;
                gSettings.loading_current = static_cast<uint32_t>(percentage * 100.f);
                gSettings.loading_name = "Reading file";
                return true;
            }
        };
    } // namespace

    ModelAssimp::ModelAssimp() = default;

    Model *ModelAssimp::Load(const std::filesystem::path &file)
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

        // 1) textures (and defaults)
        model.UploadImages(cmd);

        // 2) materials + geometry + aabbs (single coherent pass)
        model.BuildMeshes();

        // 3) nodes
        model.SetupNodes();
        model.UpdateNodeMatrices();

        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();

        // cache is only needed during build
        modelAssimp->m_textureCache.clear();

        return modelAssimp;
    }

    bool ModelAssimp::LoadFile(const std::filesystem::path &file)
    {
        m_importer.SetProgressHandler(new CustomAssimpProgressHandler());
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
        // flags |= aiProcess_MakeLeftHanded;
        flags |= aiProcess_FlipUVs;
        // flags |= aiProcess_FlipWindingOrder;

        m_scene = m_importer.ReadFile(file.string(), flags);
        if (!m_scene || (m_scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !m_scene->mRootNode)
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

        loading = "Loading textures";
        progress = 0;

        ResetResources(cmd);

        // texture types we consider when preloading
        const std::initializer_list<aiTextureType> preloadTypes = {
            aiTextureType_BASE_COLOR,
            aiTextureType_DIFFUSE,
            aiTextureType_NORMALS,
            aiTextureType_NORMAL_CAMERA,
            aiTextureType_HEIGHT,
            aiTextureType_UNKNOWN,
            aiTextureType_SPECULAR,
            aiTextureType_LIGHTMAP,
            aiTextureType_AMBIENT_OCCLUSION,
            aiTextureType_EMISSIVE,
            aiTextureType_EMISSION_COLOR,
            aiTextureType_METALNESS,
            aiTextureType_DIFFUSE_ROUGHNESS,
        };

        // count unique textures for UI
        std::unordered_set<std::string> uniqueTexKeys;
        uniqueTexKeys.reserve(m_scene->mNumMaterials * 4);

        for (unsigned int i = 0; i < m_scene->mNumMaterials; i++)
        {
            aiMaterial *material = m_scene->mMaterials[i];
            for (aiTextureType type : preloadTypes)
            {
                unsigned int textureCount = material->GetTextureCount(type);
                for (unsigned int j = 0; j < textureCount; j++)
                {
                    std::filesystem::path texPath = GetTexturePath(material, type, static_cast<int>(j));
                    if (texPath.empty())
                        continue;
                    uniqueTexKeys.insert(texPath.generic_string());
                }
            }
        }

        total = static_cast<uint32_t>(uniqueTexKeys.size());

        // load all unique
        for (const std::string &key : uniqueTexKeys)
        {
            Image *img = LoadTexture(cmd, key);
            if (img)
                progress++;
        }

        // NOTE: defaults already added via ResetResources()
    }

    void ModelAssimp::BuildMeshes()
    {
        const auto &defaults = GetDefaultResources();

        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading_current;
        auto &total = gSettings.loading_total;
        auto &loading = gSettings.loading_name;

        loading = "Loading materials + geometry";
        progress = 0;

        m_meshInfos.clear();
        m_meshInfos.resize(m_scene->mNumMeshes);

        m_meshCount = 0;
        m_verticesCount = 0;
        m_indicesCount = 0;

        for (unsigned int i = 0; i < m_scene->mNumMeshes; i++)
        {
            const aiMesh *mesh = m_scene->mMeshes[i];
            if (!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE))
                continue;

            MeshInfo &mi = m_meshInfos[i];

            mi.vertexOffset = m_verticesCount;
            mi.verticesCount = mesh->mNumVertices;

            mi.indexOffset = m_indicesCount;
            mi.indicesCount = mesh->mNumFaces * 3;

            mi.aabbVertexOffset = static_cast<size_t>(m_meshCount) * 8;

            m_verticesCount += mesh->mNumVertices;
            m_indicesCount += mesh->mNumFaces * 3;
            m_meshCount++;
        }

        total = m_verticesCount + m_indicesCount;

        m_vertices.clear();
        m_positionUvs.clear();
        m_aabbVertices.clear();
        m_indices.clear();

        m_vertices.reserve(m_verticesCount);
        m_positionUvs.reserve(m_verticesCount);
        m_indices.reserve(m_indicesCount);
        m_aabbVertices.reserve(static_cast<size_t>(m_meshCount) * 8);

        // pass 2: build per mesh
        for (unsigned int i = 0; i < m_scene->mNumMeshes; i++)
        {
            const aiMesh *mesh = m_scene->mMeshes[i];
            if (!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE))
                continue;

            MeshInfo &mi = m_meshInfos[i];

            // pipeline/material info
            mi.materialInfo.topology = vk::PrimitiveTopology::eTriangleList;

            aiMaterial *material = m_scene->mMaterials[mesh->mMaterialIndex];

            int twoSided = 0;
            material->Get(AI_MATKEY_TWOSIDED, twoSided);
            mi.materialInfo.cullMode = twoSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eFront;

            mi.renderType = DetermineRenderType(material);
            mi.materialInfo.alphaBlend = (mi.renderType != RenderType::Opaque);

            // defaults
            mi.images[static_cast<int>(TextureType::BaseColor)] = defaults.black;
            mi.images[static_cast<int>(TextureType::MetallicRoughness)] = defaults.black;
            mi.images[static_cast<int>(TextureType::Normal)] = defaults.normal;
            mi.images[static_cast<int>(TextureType::Occlusion)] = defaults.white;
            mi.images[static_cast<int>(TextureType::Emissive)] = defaults.black;

            for (auto &s : mi.samplers)
                s = defaults.sampler;

            mi.textureMask = 0;

            // assign textures (from cache loaded in UploadImages)
            AssignTexture(mi, TextureType::BaseColor, material, {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE});
            AssignTexture(mi, TextureType::MetallicRoughness, material, {aiTextureType_UNKNOWN, aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_SPECULAR, aiTextureType_METALNESS});
            AssignTexture(mi, TextureType::Normal, material, {aiTextureType_NORMAL_CAMERA, aiTextureType_NORMALS, aiTextureType_HEIGHT});
            AssignTexture(mi, TextureType::Occlusion, material, {aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP});
            AssignTexture(mi, TextureType::Emissive, material, {aiTextureType_EMISSION_COLOR, aiTextureType_EMISSIVE});

            ComputeMaterialData(mi, material);

            // geometry
            const aiVector3D *positions = mesh->mVertices;
            const aiVector3D *normals = mesh->mNormals;
            const aiVector3D *uvs = mesh->mTextureCoords[0];
            const aiColor4D *colors = mesh->mColors[0];

            // bounding box (local)
            bool bbInit = false;
            vec3 bbMin(0.f), bbMax(0.f);

            for (unsigned int v = 0; v < mesh->mNumVertices; v++)
            {
                Vertex vertex{};
                PositionUvVertex positionUvVertex{};

                const aiVector3D &p = positions[v];
                FillVertexPosition(vertex, p.x, p.y, p.z);
                FillVertexPosition(positionUvVertex, p.x, p.y, p.z);

                if (uvs)
                {
                    FillVertexUV(vertex, uvs[v].x, uvs[v].y);
                    FillVertexUV(positionUvVertex, uvs[v].x, uvs[v].y);
                }
                else
                {
                    FillVertexUV(vertex, 0.0f, 0.0f);
                    FillVertexUV(positionUvVertex, 0.0f, 0.0f);
                }

                if (normals)
                {
                    const aiVector3D &n = normals[v];
                    FillVertexNormal(vertex, n.x, n.y, n.z);
                }
                else
                {
                    FillVertexNormal(vertex, 0.0f, 0.0f, 1.0f);
                }

                if (colors)
                {
                    const aiColor4D &c = colors[v];
                    FillVertexColor(vertex, c.r, c.g, c.b, c.a);
                }
                else
                {
                    FillVertexColor(vertex, 1.0f, 1.0f, 1.0f, 1.0f);
                }

                FillVertexJointsWeights(vertex, 0, 0, 0, 0, 0.f, 0.f, 0.f, 0.f);
                FillVertexJointsWeights(positionUvVertex, 0, 0, 0, 0, 0.f, 0.f, 0.f, 0.f);

                m_vertices.push_back(vertex);
                m_positionUvs.push_back(positionUvVertex);
                progress++;

                // update AABB
                vec3 pos(p.x, p.y, p.z);
                if (!bbInit)
                {
                    bbMin = pos;
                    bbMax = pos;
                    bbInit = true;
                }
                else
                {
                    bbMin = glm::min(bbMin, pos);
                    bbMax = glm::max(bbMax, pos);
                }
            }

            if (bbInit)
            {
                mi.boundingBox.min = bbMin;
                mi.boundingBox.max = bbMax;
            }

            // indices (kept local; vertexOffset is used in vkCmdDrawIndexed vertexOffset param)
            const size_t expected = static_cast<size_t>(mesh->mNumFaces) * 3;
            const size_t start = m_indices.size();
            m_indices.resize(start + expected);

            size_t w = start;
            for (unsigned int f = 0; f < mesh->mNumFaces; f++)
            {
                const aiFace &face = mesh->mFaces[f];
                if (face.mNumIndices != 3)
                    continue;

                m_indices[w++] = face.mIndices[0];
                m_indices[w++] = face.mIndices[1];
                m_indices[w++] = face.mIndices[2];
            }

            m_indices.resize(w);
            progress += mesh->mNumFaces;

            // aabb vertices (8 corners)
            mi.aabbColor = static_cast<uint32_t>(rand(0, 255) << 24) |
                           static_cast<uint32_t>(rand(0, 255) << 16) |
                           static_cast<uint32_t>(rand(0, 255) << 8) |
                           static_cast<uint32_t>(255);

            const vec3 &mn = mi.boundingBox.min;
            const vec3 &mx = mi.boundingBox.max;

            AabbVertex corners[8] = {
                {mn.x, mn.y, mn.z},
                {mx.x, mn.y, mn.z},
                {mx.x, mx.y, mn.z},
                {mn.x, mx.y, mn.z},
                {mn.x, mn.y, mx.z},
                {mx.x, mn.y, mx.z},
                {mx.x, mx.y, mx.z},
                {mn.x, mx.y, mx.z},
            };

            for (const auto &c : corners)
                m_aabbVertices.push_back(c);
        }
    }

    void ModelAssimp::SetupNodes()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading_current;
        auto &total = gSettings.loading_total;
        auto &loading = gSettings.loading_name;

        loading = "Loading nodes";
        progress = 0;

        int nodeCount = 0;
        CountNodesRecursive(m_scene->mRootNode, nodeCount);
        total = nodeCount;

        dirtyNodes = true;
        m_nodeInfos.clear();
        m_nodeToMesh.clear();

        m_nodeInfos.reserve(nodeCount);
        m_nodeToMesh.reserve(nodeCount);

        ProcessNode(m_scene->mRootNode, -1);

        // ensure mapping size matches
        if (m_nodeToMesh.size() < m_nodeInfos.size())
            m_nodeToMesh.resize(m_nodeInfos.size(), -1);

        progress = static_cast<uint32_t>(m_nodeInfos.size());
    }

    void ModelAssimp::ProcessNode(const aiNode *node, int parentIndex)
    {
        auto pushNode = [&](const aiNode *srcNode, int parent, const mat4 &local, int meshIdx) -> int
        {
            const int idx = static_cast<int>(m_nodeInfos.size());
            m_nodeInfos.push_back(NodeInfo{});

            NodeInfo &ni = m_nodeInfos.back();
            ni.name = (srcNode && srcNode->mName.length > 0) ? srcNode->mName.C_Str() : ("Node " + std::to_string(idx));
            ni.parent = parent;
            ni.localMatrix = local;
            ni.dirty = true;
            ni.dirtyUniforms.resize(RHII.GetSwapchainImageCount(), false);

            if (m_nodeToMesh.size() <= static_cast<size_t>(idx))
                m_nodeToMesh.resize(static_cast<size_t>(idx) + 1, -1);

            m_nodeToMesh[idx] = meshIdx;
            return idx;
        };

        const mat4 local = AiToGlmMat4(node->mTransformation);

        // this node is the transform carrier
        const int transformNodeIdx = pushNode(node, parentIndex, local, -1);

        // attach meshes (glTF-like: 1 mesh per node; extra meshes become siblings sharing the same transform)
        if (node->mNumMeshes > 0)
        {
            bool firstAssigned = false;

            for (unsigned int k = 0; k < node->mNumMeshes; ++k)
            {
                const int meshIdx = static_cast<int>(node->mMeshes[k]);
                if (meshIdx < 0 || meshIdx >= static_cast<int>(m_scene->mNumMeshes))
                    continue;

                const aiMesh *m = m_scene->mMeshes[meshIdx];
                if (!(m->mPrimitiveTypes & aiPrimitiveType_TRIANGLE))
                    continue;

                if (!firstAssigned)
                {
                    m_nodeToMesh[transformNodeIdx] = meshIdx;
                    firstAssigned = true;
                }
                else
                {
                    pushNode(node, parentIndex, local, meshIdx);
                }
            }
        }

        // children inherit transformNodeIdx
        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            ProcessNode(node->mChildren[i], transformNodeIdx);
    }

    std::filesystem::path ModelAssimp::GetTexturePath(const aiMaterial *material, aiTextureType type, int index) const
    {
        aiString path;
        if (material->GetTexture(type, index, &path) != AI_SUCCESS || path.length == 0)
            return {};

        // embedded textures like "*0" not handled here
        if (path.C_Str()[0] == '*')
            return {};

        std::filesystem::path rel = path.C_Str();

        // candidates: raw, (modelDir / rel), (modelDir / filename)
        const std::filesystem::path candidates[] = {
            rel,
            m_filePath / rel,
            m_filePath / rel.filename(),
        };

        for (const auto &c : candidates)
        {
            if (c.empty())
                continue;

            std::error_code ec;
            std::filesystem::path norm = std::filesystem::weakly_canonical(c, ec);
            if (!ec && std::filesystem::exists(norm))
                return norm;

            if (std::filesystem::exists(c))
                return c;
        }

        return {};
    }

    void ModelAssimp::AssignTexture(MeshInfo &meshInfo, TextureType type, aiMaterial *material,
                                    std::initializer_list<aiTextureType> textureTypes)
    {
        for (aiTextureType aiType : textureTypes)
        {
            std::filesystem::path texPath = GetTexturePath(material, aiType, 0);
            if (texPath.empty())
                continue;

            if (Image *loaded = GetTextureFromCache(texPath))
            {
                meshInfo.images[static_cast<int>(type)] = loaded;
                meshInfo.textureMask |= TextureBit(type);
                return;
            }
        }
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
            float specI = std::max(specular.r, std::max(specular.g, specular.b));
            metallic = glm::clamp(specI, 0.f, 1.f);
        }

        if (!hasRoughnessFactor)
        {
            float shininess = 0.f;
            if (material->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS && shininess > 0.f)
                roughness = glm::clamp(std::sqrt(2.f / (shininess + 2.f)), 0.f, 1.f);
        }

        // packing to shader expectations:
        // row0: baseColor rgba
        // row1: emissive rgb
        // row2: metallic, roughness, alphaCutoff, occlusionStrength
        // row3: (unused), normalScale, (unused)
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

        factors[3][1] = normalScale;

        meshInfo.materialFactors = factors;
        meshInfo.alphaCutoff = alphaCutoff;
    }

    RenderType ModelAssimp::DetermineRenderType(aiMaterial *material) const
    {
        aiString alphaMode;
        if (material->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS)
        {
            std::string mode(alphaMode.C_Str());
            std::transform(mode.begin(), mode.end(), mode.begin(),
                           [](unsigned char c)
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

} // namespace pe
