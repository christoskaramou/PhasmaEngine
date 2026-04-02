#include "Scene/AssimpLoader.h"
#include "Animation/AnimationImporter.h"
#include "Scene/Material.h"
#include "Scene/Scene.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Scene/ModelAsset.h" // for DefaultResources, LoadTexture, TextureType, TextureBit — will remove later
#include "Systems/RendererSystem.h"
#include <assimp/GltfMaterial.h>
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/ProgressHandler.hpp>
#include <meshoptimizer.h>

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
            return mat4(
                a.a1, a.b1, a.c1, a.d1,
                a.a2, a.b2, a.c2, a.d2,
                a.a3, a.b3, a.c3, a.d3,
                a.a4, a.b4, a.c4, a.d4);
        }

        class LoaderProgressHandler : public Assimp::ProgressHandler
        {
        public:
            bool Update(float percentage) override
            {
                auto &gSettings = Settings::Get<GlobalSettings>();
                gSettings.loading.total = 100;
                gSettings.loading.current = static_cast<uint32_t>(percentage * 100.f);
                gSettings.loading.SetName("Reading file");
                return true;
            }
        };

        static std::string DecodeURI(const std::string &uri)
        {
            std::string decoded;
            decoded.reserve(uri.length());
            for (size_t i = 0; i < uri.length(); i++)
            {
                if (uri[i] == '%' && i + 2 < uri.length())
                {
                    std::string hex = uri.substr(i + 1, 2);
                    char c = static_cast<char>(strtol(hex.c_str(), nullptr, 16));
                    decoded += c;
                    i += 2;
                }
                else if (uri[i] == '+')
                {
                    decoded += ' ';
                }
                else
                {
                    decoded += uri[i];
                }
            }
            return decoded;
        }

        class PhasmaIOStream : public Assimp::IOStream
        {
        public:
            PhasmaIOStream(const std::filesystem::path &file, const std::string &mode)
                : m_fileSize(0)
            {
                std::ios_base::openmode openMode = std::ios_base::binary;
                if (mode.find('r') != std::string::npos)
                    openMode |= std::ios_base::in;
                if (mode.find('w') != std::string::npos)
                    openMode |= std::ios_base::out;

#ifdef _WIN32
                m_stream.open(file.wstring(), openMode);
#else
                m_stream.open(file.string(), openMode);
#endif
                if (m_stream.is_open())
                {
                    m_stream.seekg(0, std::ios_base::end);
                    m_fileSize = static_cast<size_t>(m_stream.tellg());
                    m_stream.seekg(0, std::ios_base::beg);
                }
            }

            ~PhasmaIOStream() override
            {
                if (m_stream.is_open())
                    m_stream.close();
            }

            size_t Read(void *pvBuffer, size_t pSize, size_t pCount) override
            {
                m_stream.read(reinterpret_cast<char *>(pvBuffer), pSize * pCount);
                return static_cast<size_t>(m_stream.gcount()) / pSize;
            }

            size_t Write(const void *pvBuffer, size_t pSize, size_t pCount) override
            {
                m_stream.write(reinterpret_cast<const char *>(pvBuffer), pSize * pCount);
                return pSize * pCount;
            }

            aiReturn Seek(size_t pOffset, aiOrigin pOrigin) override
            {
                std::ios_base::seekdir dir = std::ios_base::beg;
                switch (pOrigin)
                {
                case aiOrigin_SET:
                    dir = std::ios_base::beg;
                    break;
                case aiOrigin_CUR:
                    dir = std::ios_base::cur;
                    break;
                case aiOrigin_END:
                    dir = std::ios_base::end;
                    break;
                default:
                    break;
                }
                m_stream.seekg(pOffset, dir);
                return m_stream.fail() ? aiReturn_FAILURE : aiReturn_SUCCESS;
            }

            size_t Tell() const override
            {
                return static_cast<size_t>(const_cast<std::fstream &>(m_stream).tellg());
            }

            size_t FileSize() const override { return m_fileSize; }
            void Flush() override { m_stream.flush(); }
            bool IsOpen() const { return m_stream.is_open(); }

        private:
            std::fstream m_stream;
            size_t m_fileSize;
        };

        class PhasmaIOSystem : public Assimp::IOSystem
        {
        public:
            bool Exists(const char *pFile) const override
            {
                std::filesystem::path path(reinterpret_cast<const char8_t *>(pFile));
                return std::filesystem::exists(path);
            }

            char getOsSeparator() const override
            {
#ifdef _WIN32
                return '\\';
#else
                return '/';
#endif
            }

            Assimp::IOStream *Open(const char *pFile, const char *pMode = "rb") override
            {
                std::filesystem::path path(reinterpret_cast<const char8_t *>(pFile));
                PhasmaIOStream *stream = new PhasmaIOStream(path, pMode);
                if (!stream->IsOpen())
                {
                    delete stream;
                    return nullptr;
                }
                return stream;
            }

            void Close(Assimp::IOStream *pFile) override { delete pFile; }
        };
    } // namespace

    AssimpLoader::AssimpLoader(Scene &scene)
        : m_scene(scene)
    {
    }

    NodeId *AssimpLoader::Load(Scene &scene, const std::filesystem::path &file)
    {
        auto fileU8 = file.u8string();
        std::string fileStr(reinterpret_cast<const char *>(fileU8.c_str()));
        if (!std::filesystem::exists(file))
        {
            PE_INFO("Model file not found: %s", fileStr.c_str());
            return nullptr;
        }

        AssimpLoader loader(scene);
        auto &gSettings = Settings::Get<GlobalSettings>();
        gSettings.loading.SetName("Reading from file");

        if (!loader.LoadFile(file))
            return nullptr;

        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();

        loader.UploadImages(cmd);
        loader.BuildMeshes();
        loader.SetupNodes();
        loader.ExtractAnimations();

        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();

        scene.UpdateNodeMatrices();

        return loader.m_rootNode;
    }

    bool AssimpLoader::LoadFile(const std::filesystem::path &file)
    {
        m_importer.SetProgressHandler(new LoaderProgressHandler());
        m_filePath = file;

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
        flags |= aiProcess_FlipUVs;

        auto fileU8 = file.u8string();
        std::string fileStr(reinterpret_cast<const char *>(fileU8.c_str()));

        m_importer.SetIOHandler(new PhasmaIOSystem());
        m_aiScene = m_importer.ReadFile(fileStr, flags);

        if (!m_aiScene || (m_aiScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !m_aiScene->mRootNode)
        {
            PE_WARN("[AssimpLoader] Assimp error: %s", m_importer.GetErrorString());
            return false;
        }

        return true;
    }

    void AssimpLoader::UploadImages(CommandBuffer *cmd)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading.current;
        auto &total = gSettings.loading.total;
        gSettings.loading.SetName("Loading textures");
        progress = 0;

        // Use ModelAsset's default resources for now (will decouple later)
        ModelAsset::GetDefaultResources(cmd);

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

        std::unordered_set<std::string> uniqueTexKeys;
        uniqueTexKeys.reserve(m_aiScene->mNumMaterials * 4);

        for (unsigned int i = 0; i < m_aiScene->mNumMaterials; i++)
        {
            aiMaterial *material = m_aiScene->mMaterials[i];
            for (aiTextureType type : preloadTypes)
            {
                unsigned int textureCount = material->GetTextureCount(type);
                for (unsigned int j = 0; j < textureCount; j++)
                {
                    std::filesystem::path texPath = GetTexturePath(material, type, static_cast<int>(j));
                    if (texPath.empty())
                        continue;
                    auto texPathU8 = texPath.u8string();
                    uniqueTexKeys.insert(std::string(reinterpret_cast<const char *>(texPathU8.c_str())));
                }
            }
        }

        total = static_cast<uint32_t>(uniqueTexKeys.size());

        for (const std::string &key : uniqueTexKeys)
        {
            if (key.length() > 0 && key[0] == '*')
            {
                int textureIndex = std::stoi(key.substr(1));
                if (textureIndex < static_cast<int>(m_aiScene->mNumTextures))
                {
                    aiTexture *tex = m_aiScene->mTextures[textureIndex];
                    Image *rawImg = nullptr;
                    if (tex->mHeight == 0)
                        rawImg = Image::LoadRGBA8FromMemory(cmd, tex->pcData, tex->mWidth);
                    else
                        rawImg = Image::LoadRawFromMemory(cmd, tex->pcData, tex->mWidth, tex->mHeight, vk::Format::eB8G8R8A8Unorm);

                    if (rawImg)
                    {
                        std::shared_ptr<Image> sharedImage(rawImg, [](Image *img)
                                                           { Image::Destroy(img); });
                        ResourceManager::Get().Register<Image>(key, sharedImage);
                        m_scene.m_imageStore.push_back(ResourceHandle<Image>(sharedImage));
                        progress++;
                    }
                }
            }
            else
            {
                std::filesystem::path pathKey(reinterpret_cast<const char8_t *>(key.c_str()));
                // Use ModelAsset's LoadTexture for now (shared texture cache)
                ResourceHandle<Image> img = ResourceManager::Get().Find<Image>(pathKey.string());
                if (!img)
                {
                    // Load via the static helper
                    ModelAsset tempLoader;
                    img = tempLoader.LoadTexture(cmd, pathKey);
                }
                if (img)
                {
                    m_scene.m_imageStore.push_back(img);
                    progress++;
                }
            }
        }
    }

    void AssimpLoader::BuildMeshes()
    {
        const auto &defaults = ModelAsset::GetDefaultResources();

        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading.current;
        auto &total = gSettings.loading.total;
        gSettings.loading.SetName("Loading materials + geometry");
        progress = 0;

        m_assimpToSceneMesh.resize(m_aiScene->mNumMeshes, -1);

        size_t estimatedVertices = 0;
        size_t estimatedIndices = 0;
        for (unsigned int i = 0; i < m_aiScene->mNumMeshes; i++)
        {
            if (m_aiScene->mMeshes[i]->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)
            {
                estimatedVertices += m_aiScene->mMeshes[i]->mNumVertices;
                estimatedIndices += m_aiScene->mMeshes[i]->mNumFaces * 3;
            }
        }

        total = static_cast<uint32_t>(estimatedVertices + estimatedIndices);

        auto &vertices = m_scene.m_vertexStore;
        auto &posUvs = m_scene.m_positionUvStore;
        auto &aabbVerts = m_scene.m_aabbVertexStore;
        auto &indices = m_scene.m_indexStore;

        // Material deduplication: share Material* across meshes with the same aiMaterialIndex
        std::unordered_map<unsigned int, Material *> materialByIndex;

        for (unsigned int i = 0; i < m_aiScene->mNumMeshes; i++)
        {
            const aiMesh *aiMesh = m_aiScene->mMeshes[i];
            if (!(aiMesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE))
                continue;

            aiMaterial *material = m_aiScene->mMaterials[aiMesh->mMaterialIndex];

            Mesh mesh{};
            RenderType rt = DetermineRenderType(material);
            mesh.renderType = rt;

            // Reuse existing Material if we already created one for this aiMaterialIndex
            auto it = materialByIndex.find(aiMesh->mMaterialIndex);
            if (it != materialByIndex.end())
            {
                mesh.material = it->second;
            }
            else
            {
                auto mat = std::make_unique<Material>();
                mat->renderType = rt;
                mat->textures[static_cast<int>(TextureType::BaseColor)] = ResourceHandle<Image>::FromRaw(defaults.white);
                mat->textures[static_cast<int>(TextureType::MetallicRoughness)] = ResourceHandle<Image>::FromRaw(defaults.black);
                mat->textures[static_cast<int>(TextureType::Normal)] = ResourceHandle<Image>::FromRaw(defaults.normal);
                mat->textures[static_cast<int>(TextureType::Occlusion)] = ResourceHandle<Image>::FromRaw(defaults.white);
                mat->textures[static_cast<int>(TextureType::Emissive)] = ResourceHandle<Image>::FromRaw(defaults.black);
                for (auto &s : mat->samplers)
                    s = defaults.sampler;
                mat->textureMask = 0;

                int twoSided = 0;
                material->Get(AI_MATKEY_TWOSIDED, twoSided);
                mat->doubleSided = twoSided != 0;

                AssignTexture(*mat, TextureType::BaseColor, material, {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE});
                AssignTexture(*mat, TextureType::MetallicRoughness, material, {aiTextureType_UNKNOWN, aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_SPECULAR, aiTextureType_METALNESS});
                AssignTexture(*mat, TextureType::Normal, material, {aiTextureType_NORMAL_CAMERA, aiTextureType_NORMALS, aiTextureType_HEIGHT});
                AssignTexture(*mat, TextureType::Occlusion, material, {aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP});
                AssignTexture(*mat, TextureType::Emissive, material, {aiTextureType_EMISSION_COLOR, aiTextureType_EMISSIVE});

                ComputeMaterialData(*mat, material);

                mesh.material = mat.get();
                materialByIndex[aiMesh->mMaterialIndex] = mat.get();
                m_scene.m_ownedMaterials.push_back(std::move(mat));
            }

            // Geometry
            const aiVector3D *positions = aiMesh->mVertices;
            const aiVector3D *normals = aiMesh->mNormals;
            const aiVector3D *uvs = aiMesh->mTextureCoords[0];
            const aiColor4D *colors = aiMesh->mColors[0];

            std::vector<Vertex> meshVertices;
            std::vector<PositionUvVertex> meshPosUvs;
            meshVertices.reserve(aiMesh->mNumVertices);
            meshPosUvs.reserve(aiMesh->mNumVertices);

            bool bbInit = false;
            vec3 bbMin(0.f), bbMax(0.f);

            for (unsigned int v = 0; v < aiMesh->mNumVertices; v++)
            {
                Vertex vertex{};
                PositionUvVertex posUv{};

                const aiVector3D &p = positions[v];
                FillVertexPosition(vertex, p.x, p.y, p.z);
                FillVertexPosition(posUv, p.x, p.y, p.z);

                if (uvs)
                {
                    FillVertexUV(vertex, uvs[v].x, uvs[v].y);
                    FillVertexUV(posUv, uvs[v].x, uvs[v].y);
                }
                else
                {
                    FillVertexUV(vertex, 0.0f, 0.0f);
                    FillVertexUV(posUv, 0.0f, 0.0f);
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

                if (aiMesh->mTangents)
                {
                    const aiVector3D &t = aiMesh->mTangents[v];
                    const aiVector3D &b = aiMesh->mBitangents[v];
                    const aiVector3D &n = normals ? normals[v] : aiVector3D(0.0f, 0.0f, 1.0f);
                    float det = (n.y * t.z - n.z * t.y) * b.x + (n.z * t.x - n.x * t.z) * b.y + (n.x * t.y - n.y * t.x) * b.z;
                    float sign = (det < 0.0f) ? -1.0f : 1.0f;
                    FillVertexTangent(vertex, t.x, t.y, t.z, sign);
                }
                else
                {
                    FillVertexTangent(vertex, 1.0f, 0.0f, 0.0f, 1.0f);
                }

                FillVertexJointsWeights(vertex, 0, 0, 0, 0, 1.f, 0.f, 0.f, 0.f);
                FillVertexJointsWeights(posUv, 0, 0, 0, 0, 1.f, 0.f, 0.f, 0.f);

                meshVertices.push_back(vertex);
                meshPosUvs.push_back(posUv);
                progress++;

                vec3 pos(p.x, p.y, p.z);
                if (!bbInit)
                {
                    bbMin = pos;
                    bbMax = pos;
                    bbInit = true;
                }
                else
                {
                    bbMin = min(bbMin, pos);
                    bbMax = max(bbMax, pos);
                }
            }

            if (bbInit)
            {
                mesh.boundingBox.min = bbMin;
                mesh.boundingBox.max = bbMax;
            }

            // Extract bone weights from aiMesh
            AnimationImporter::ExtractBoneWeights(aiMesh, m_scene.m_skeleton, meshVertices, meshPosUvs);
            if (aiMesh->mNumBones > 0)
                mesh.skinned = true;

            // Indices
            std::vector<unsigned int> meshIndices;
            meshIndices.reserve(aiMesh->mNumFaces * 3);
            for (unsigned int f = 0; f < aiMesh->mNumFaces; f++)
            {
                const aiFace &face = aiMesh->mFaces[f];
                if (face.mNumIndices == 3)
                {
                    meshIndices.push_back(face.mIndices[0]);
                    meshIndices.push_back(face.mIndices[1]);
                    meshIndices.push_back(face.mIndices[2]);
                }
            }

            // Meshoptimizer
            std::vector<unsigned int> remap(meshIndices.size());
            size_t vertCount = meshopt_generateVertexRemap(
                remap.data(), meshIndices.data(), meshIndices.size(),
                meshVertices.data(), meshVertices.size(), sizeof(Vertex));

            std::vector<unsigned int> remappedIndices(meshIndices.size());
            std::vector<Vertex> remappedVerts(vertCount);
            std::vector<PositionUvVertex> remappedPosUvs(vertCount);

            meshopt_remapIndexBuffer(remappedIndices.data(), meshIndices.data(), meshIndices.size(), remap.data());
            meshopt_remapVertexBuffer(remappedVerts.data(), meshVertices.data(), meshVertices.size(), sizeof(Vertex), remap.data());
            meshopt_remapVertexBuffer(remappedPosUvs.data(), meshPosUvs.data(), meshPosUvs.size(), sizeof(PositionUvVertex), remap.data());

            meshopt_optimizeVertexCache(remappedIndices.data(), remappedIndices.data(), remappedIndices.size(), vertCount);
            meshopt_optimizeOverdraw(remappedIndices.data(), remappedIndices.data(), remappedIndices.size(),
                                     reinterpret_cast<float *>(remappedVerts.data()), vertCount, sizeof(Vertex), 1.05f);

            std::vector<unsigned int> fetchRemap(vertCount);
            size_t uniqueVerts = meshopt_optimizeVertexFetchRemap(fetchRemap.data(), remappedIndices.data(), remappedIndices.size(), vertCount);

            meshopt_remapIndexBuffer(remappedIndices.data(), remappedIndices.data(), remappedIndices.size(), fetchRemap.data());

            std::vector<Vertex> finalVerts(uniqueVerts);
            std::vector<PositionUvVertex> finalPosUvs(uniqueVerts);
            meshopt_remapVertexBuffer(finalVerts.data(), remappedVerts.data(), vertCount, sizeof(Vertex), fetchRemap.data());
            meshopt_remapVertexBuffer(finalPosUvs.data(), remappedPosUvs.data(), vertCount, sizeof(PositionUvVertex), fetchRemap.data());

            // Fill mesh descriptor with offsets into Scene's data stores
            mesh.vertexOffset = static_cast<uint32_t>(vertices.size());
            mesh.vertexCount = static_cast<uint32_t>(uniqueVerts);
            mesh.indexOffset = static_cast<uint32_t>(indices.size());
            mesh.indexCount = static_cast<uint32_t>(remappedIndices.size());
            mesh.aabbVertexOffset = aabbVerts.size();

            // Push to Scene's data stores
            vertices.insert(vertices.end(), finalVerts.begin(), finalVerts.end());
            posUvs.insert(posUvs.end(), finalPosUvs.begin(), finalPosUvs.end());
            indices.insert(indices.end(), remappedIndices.begin(), remappedIndices.end());

            progress += aiMesh->mNumFaces;

            // AABB vertices (8 corners for debug visualization)
            mesh.aabbColor = static_cast<uint32_t>(rand(0, 255) << 24) |
                             static_cast<uint32_t>(rand(0, 255) << 16) |
                             static_cast<uint32_t>(rand(0, 255) << 8) |
                             static_cast<uint32_t>(255);

            const vec3 &mn = mesh.boundingBox.min;
            const vec3 &mx = mesh.boundingBox.max;
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
                aabbVerts.push_back(c);

            // Register mesh in Scene and record the mapping
            m_assimpToSceneMesh[i] = m_scene.AddMesh(std::move(mesh));
        }
    }

    void AssimpLoader::SetupNodes()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading.current;
        auto &total = gSettings.loading.total;
        gSettings.loading.SetName("Loading nodes");
        progress = 0;

        int nodeCount = 0;
        CountNodesRecursive(m_aiScene->mRootNode, nodeCount);
        total = nodeCount;

        ProcessNode(m_aiScene->mRootNode, nullptr);

        progress = nodeCount;
    }

    void AssimpLoader::ProcessNode(const aiNode *node, NodeId *parent)
    {
        std::string nodeName = (node->mName.length > 0)
                                   ? node->mName.C_Str()
                                   : ("Node " + std::to_string(m_scene.GetNodeCount()));

        const mat4 local = AiToGlmMat4(node->mTransformation);

        // Create the transform carrier node
        NodeId *transformNode = m_scene.CreateNode(nodeName, parent);
        m_scene.SetLocalMatrix(transformNode, local, false);

        if (!m_rootNode)
            m_rootNode = transformNode;

        // Assign meshes
        if (node->mNumMeshes > 0)
        {
            bool firstAssigned = false;

            for (unsigned int k = 0; k < node->mNumMeshes; ++k)
            {
                const int assimpIdx = static_cast<int>(node->mMeshes[k]);
                if (assimpIdx < 0 || assimpIdx >= static_cast<int>(m_aiScene->mNumMeshes))
                    continue;

                const int sceneMeshIdx = m_assimpToSceneMesh[assimpIdx];
                if (sceneMeshIdx < 0)
                    continue;

                if (!firstAssigned)
                {
                    m_scene.SetMeshRef(transformNode, sceneMeshIdx);
                    firstAssigned = true;
                }
                else
                {
                    // Extra meshes get child nodes with identity transform
                    NodeId *extraNode = m_scene.CreateNode(nodeName + "_mesh" + std::to_string(k), transformNode);
                    m_scene.SetMeshRef(extraNode, sceneMeshIdx);
                }
            }
        }

        // Recurse children
        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            ProcessNode(node->mChildren[i], transformNode);
    }

    std::filesystem::path AssimpLoader::GetTexturePath(const aiMaterial *material, aiTextureType type, int index) const
    {
        aiString path;
        if (material->GetTexture(type, index, &path) != AI_SUCCESS || path.length == 0)
            return {};

        if (path.C_Str()[0] == '*')
            return std::string(path.C_Str());

        std::string pathStr = DecodeURI(path.C_Str());
        if (!pathStr.empty() && (pathStr[0] == '/' || pathStr[0] == '\\'))
            pathStr = pathStr.substr(1);

        std::filesystem::path rel(reinterpret_cast<const char8_t *>(pathStr.c_str()));

        auto AppendCandidate = [](std::vector<std::filesystem::path> &out, const std::filesystem::path &candidate)
        {
            if (candidate.empty())
                return;
            out.push_back(candidate);

            std::string ext = candidate.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });

            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp")
            {
                std::filesystem::path ddsCandidate = candidate;
                ddsCandidate.replace_extension(".dds");
                out.push_back(std::move(ddsCandidate));
            }
        };

        std::vector<std::filesystem::path> candidates;
        candidates.reserve(6);
        AppendCandidate(candidates, rel);
        AppendCandidate(candidates, m_filePath.parent_path() / rel);
        AppendCandidate(candidates, m_filePath.parent_path() / rel.filename());

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

        PE_WARN("[AssimpLoader] Failed to find texture, using default: %s", pathStr.c_str());
        return {};
    }

    void AssimpLoader::AssignTexture(Material &mat, TextureType type, aiMaterial *material,
                                     std::initializer_list<aiTextureType> textureTypes)
    {
        for (aiTextureType aiType : textureTypes)
        {
            std::filesystem::path texPath = GetTexturePath(material, aiType, 0);
            if (texPath.empty())
                continue;

            ResourceHandle<Image> loaded = ResourceManager::Get().Find<Image>(texPath.string());
            if (loaded)
            {
                mat.textures[static_cast<int>(type)] = loaded;
                mat.textureMask |= TextureBit(type);
                return;
            }
        }
    }

    void AssimpLoader::ComputeMaterialData(Material &mat, aiMaterial *material) const
    {
        if (!material)
            return;

        const bool hasNormalMap = (mat.textureMask & TextureBit(TextureType::Normal)) != 0;
        const bool hasOcclusionMap = (mat.textureMask & TextureBit(TextureType::Occlusion)) != 0;

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

        float alphaCutoff = 0.5f;
#ifdef AI_MATKEY_GLTF_ALPHACUTOFF
        material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, alphaCutoff);
#endif

        if (!hasMetallicFactor)
        {
            float specI = std::max(specular.r, std::max(specular.g, specular.b));
            metallic = clamp(specI, 0.f, 1.f);
        }

        if (!hasRoughnessFactor)
        {
            float shininess = 0.f;
            if (material->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS && shininess > 0.f)
                roughness = clamp(std::sqrt(2.f / (shininess + 2.f)), 0.f, 1.f);
        }

        float transmissionFactor = 0.f;
#ifdef AI_MATKEY_TRANSMISSION_FACTOR
        material->Get(AI_MATKEY_TRANSMISSION_FACTOR, transmissionFactor);
#endif

        float thicknessFactor = 0.f;
#ifdef AI_MATKEY_VOLUME_THICKNESS_FACTOR
        material->Get(AI_MATKEY_VOLUME_THICKNESS_FACTOR, thicknessFactor);
#endif

        float attenuationDistance = std::numeric_limits<float>::infinity();
#ifdef AI_MATKEY_VOLUME_ATTENUATION_DISTANCE
        material->Get(AI_MATKEY_VOLUME_ATTENUATION_DISTANCE, attenuationDistance);
#endif

        aiColor3D attenuationColor(1.f, 1.f, 1.f);
#ifdef AI_MATKEY_VOLUME_ATTENUATION_COLOR
        material->Get(AI_MATKEY_VOLUME_ATTENUATION_COLOR, attenuationColor);
#endif

        float ior = 1.5f;
        material->Get(AI_MATKEY_REFRACTI, ior);

        aiString aiName;
        if (material->Get(AI_MATKEY_NAME, aiName) == AI_SUCCESS)
            mat.name = aiName.C_Str();

        mat.baseColorFactor = vec4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);
        mat.emissiveFactor = vec3(emissive.r, emissive.g, emissive.b);
        mat.metallic = metallic;
        mat.roughness = roughness;
        mat.alphaCutoff = alphaCutoff;
        mat.occlusionStrength = occlusionStrength;
        mat.normalScale = normalScale;
        mat.transmissionFactor = transmissionFactor;
        mat.thicknessFactor = thicknessFactor;
        mat.attenuationDistance = attenuationDistance;
        mat.ior = ior;
        mat.attenuationColor = vec3(attenuationColor.r, attenuationColor.g, attenuationColor.b);
    }

    RenderType AssimpLoader::DetermineRenderType(aiMaterial *material) const
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

        float transmissionFactor = 0.f;
#ifdef AI_MATKEY_TRANSMISSION_FACTOR
        material->Get(AI_MATKEY_TRANSMISSION_FACTOR, transmissionFactor);
#endif
        if (transmissionFactor > 0.f)
            return RenderType::Transmission;

        return RenderType::Opaque;
    }

    void AssimpLoader::ExtractAnimations()
    {
        if (!m_aiScene)
            return;

        AnimationImporter::ResolveBoneParents(m_aiScene, m_scene.m_skeleton);
        AnimationImporter::ExtractAnimationClips(m_aiScene, m_scene.m_skeleton, m_scene.m_animationClips);
    }
} // namespace pe
