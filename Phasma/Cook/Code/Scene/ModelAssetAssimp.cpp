#include "Scene/ModelAssetAssimp.h"
#include "Animation/AnimationImporter.h"
#include "Scene/Material.h"
#include "Scene/PassInfoAsset.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
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
                auto &gSettings = Settings::Get<SceneSettings>();
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
                    break; // Handle _AI_ORIGIN_ENFORCE_ENUM_SIZE
                }
                m_stream.seekg(pOffset, dir);
                return m_stream.fail() ? aiReturn_FAILURE : aiReturn_SUCCESS;
            }

            size_t Tell() const override
            {
                return static_cast<size_t>(const_cast<std::fstream &>(m_stream).tellg());
            }

            size_t FileSize() const override
            {
                return m_fileSize;
            }

            void Flush() override
            {
                m_stream.flush();
            }

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

            void Close(Assimp::IOStream *pFile) override
            {
                delete pFile;
            }
        };
    } // namespace

    ModelAssetAssimp::ModelAssetAssimp() = default;

    ModelAsset *ModelAssetAssimp::Load(const std::filesystem::path &file)
    {
        // Full synchronous load (CPU + GPU on calling thread)
        ModelAsset *model = LoadCpuOnly(file);
        if (model)
            static_cast<ModelAssetAssimp *>(model)->UploadGpu();
        return model;
    }

    ModelAsset *ModelAssetAssimp::LoadCpuOnly(const std::filesystem::path &file)
    {
        auto fileU8 = file.u8string();
        std::string fileStr(reinterpret_cast<const char *>(fileU8.c_str()));
        if (!std::filesystem::exists(file))
        {
            PE_INFO("ModelAsset file not found: %s", fileStr.c_str());
            return nullptr;
        }

        ModelAssetAssimp *modelAssimp = new ModelAssetAssimp();
        ModelAssetAssimp &model = *modelAssimp;
        auto labelU8 = file.filename().u8string();
        model.SetLabel(std::string(reinterpret_cast<const char *>(labelU8.c_str())));

        auto &gSettings = Settings::Get<SceneSettings>();
        gSettings.loading.SetName("Reading from file");

        if (!model.LoadFile(file))
        {
            delete modelAssimp;
            return nullptr;
        }

        // Only Assimp file parsing on background thread.
        // All GPU work + mesh building deferred to UploadGpu() on main thread.

        return modelAssimp;
    }

    void ModelAssetAssimp::UploadGpu()
    {
        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();

        // 1) textures (registers in ResourceManager)
        UploadImages(cmd);

        // 2) materials + geometry + aabbs (reads texture handles from ResourceManager)
        BuildMeshes();

        // 3) nodes
        SetupNodes();
        ExtractAnimations();

        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();
    }

    bool ModelAssetAssimp::LoadFile(const std::filesystem::path &file)
    {
        m_importer.SetProgressHandler(new CustomAssimpProgressHandler());
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
        // flags |= aiProcess_PreTransformVertices;
        // flags |= aiProcess_OptimizeMeshes;
        // flags |= aiProcess_FindInstances;
        // flags |= aiProcess_MakeLeftHanded;
        flags |= aiProcess_FlipUVs;
        // flags |= aiProcess_FlipWindingOrder;

        auto fileU8 = file.u8string();
        std::string fileStr(reinterpret_cast<const char *>(fileU8.c_str()));

        // Use custom IOSystem to handle unicode paths on Windows
        m_importer.SetIOHandler(new PhasmaIOSystem());

        m_scene = m_importer.ReadFile(fileStr, flags);

        // aiProcess_ValidateDataStructure is the only post-process step that can reject a scene
        // (reset it to NULL or flag it AI_SCENE_FLAGS_INCOMPLETE). It is overly strict and rejects
        // otherwise-valid Khronos sample assets over non-geometry nits (e.g. SpecularTest's
        // out-of-range specular texture index, TransmissionSuzanne's light/name collision). For a
        // cook tool we'd rather salvage the geometry, so on a validation rejection retry once
        // WITHOUT the validation pass and cook whatever Assimp parsed.
        if (!m_scene || (m_scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !m_scene->mRootNode)
        {
            PE_WARN("[ModelAsset] Assimp validation rejected model; retrying without validation: %s",
                    m_importer.GetErrorString());
            m_scene = m_importer.ReadFile(fileStr, flags & ~aiProcess_ValidateDataStructure);
        }

        if (!m_scene || !m_scene->mRootNode)
        {
            PE_WARN("[ModelAsset] Assimp error: %s", m_importer.GetErrorString());
            return false;
        }

        return true;
    }

    void ModelAssetAssimp::UploadImages(CommandBuffer *cmd)
    {
        auto &gSettings = Settings::Get<SceneSettings>();
        auto &progress = gSettings.loading.current;
        auto &total = gSettings.loading.total;

        gSettings.loading.SetName("Loading textures");
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
                    auto texPathU8 = texPath.u8string();
                    uniqueTexKeys.insert(std::string(reinterpret_cast<const char *>(texPathU8.c_str())));
                }
            }
        }

        total = static_cast<uint32_t>(uniqueTexKeys.size());

        // load all unique
        for (const std::string &key : uniqueTexKeys)
        {
            if (key.length() > 0 && key[0] == '*')
            {
                int textureIndex = std::stoi(key.substr(1));
                if (textureIndex < m_scene->mNumTextures)
                {
                    aiTexture *tex = m_scene->mTextures[textureIndex];
                    Image *rawImg = nullptr;
                    if (tex->mHeight == 0)
                    {
                        rawImg = Image::LoadRGBA8FromMemory(cmd, tex->pcData, tex->mWidth);
                    }
                    else
                    {
                        rawImg = Image::LoadRawFromMemory(cmd, tex->pcData, tex->mWidth, tex->mHeight, PE_FORMAT_B8G8R8A8_UNORM);
                    }

                    if (rawImg)
                    {
                        std::shared_ptr<Image> sharedImage(rawImg, [](Image *img)
                                                           { Image::Destroy(img); });
                        ResourceManager::Get().Register<Image>(key, sharedImage);
                        m_images.push_back(ResourceHandle<Image>(sharedImage));
                        progress++;
                    }
                }
            }
            else
            {
                std::filesystem::path pathKey(reinterpret_cast<const char8_t *>(key.c_str()));
                ResourceHandle<Image> img = LoadTexture(cmd, pathKey);
                if (img)
                    progress++;
            }
        }

        // NOTE: defaults already added via ResetResources()
    }

    void ModelAssetAssimp::BuildMeshes()
    {
        const auto &defaults = GetDefaultResources();

        auto &gSettings = Settings::Get<SceneSettings>();
        auto &progress = gSettings.loading.current;
        auto &total = gSettings.loading.total;

        gSettings.loading.SetName("Loading materials + geometry");
        progress = 0;

        m_meshInfos.clear();
        m_meshInfos.resize(m_scene->mNumMeshes);

        size_t estimatedVertices = 0;
        size_t estimatedIndices = 0;
        for (unsigned int i = 0; i < m_scene->mNumMeshes; i++)
        {
            if (m_scene->mMeshes[i]->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)
            {
                estimatedVertices += m_scene->mMeshes[i]->mNumVertices;
                estimatedIndices += m_scene->mMeshes[i]->mNumFaces * 3;
            }
        }

        total = estimatedVertices + estimatedIndices; // progress total

        m_vertices.clear();
        m_positionUvs.clear();
        m_aabbVertices.clear();
        m_indices.clear();

        m_vertices.reserve(estimatedVertices);
        m_positionUvs.reserve(estimatedVertices);
        m_indices.reserve(estimatedIndices);
        m_aabbVertices.reserve(static_cast<size_t>(m_meshCount) * 8);

        // Material deduplication: share Material* across meshes with the same aiMaterialIndex
        std::unordered_map<unsigned int, Material *> materialByIndex;

        // pass 2: build per mesh
        for (unsigned int i = 0; i < m_scene->mNumMeshes; i++)
        {
            const aiMesh *mesh = m_scene->mMeshes[i];
            if (!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE))
                continue;

            aiMaterial *material = m_scene->mMaterials[mesh->mMaterialIndex];

            MeshInfo &mi = m_meshInfos[i];
            RenderType rt = DetermineRenderType(material);
            mi.renderType = rt;

            // Reuse existing Material if we already created one for this aiMaterialIndex
            auto it = materialByIndex.find(mesh->mMaterialIndex);
            if (it != materialByIndex.end())
            {
                mi.material = it->second;
            }
            else
            {
                int twoSided = 0;
                material->Get(AI_MATKEY_TWOSIDED, twoSided);

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
                mat->doubleSided = twoSided != 0;

                AssignTexture(*mat, TextureType::BaseColor, material, {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE});
                AssignTexture(*mat, TextureType::MetallicRoughness, material, {aiTextureType_UNKNOWN, aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_SPECULAR, aiTextureType_METALNESS});
                AssignTexture(*mat, TextureType::Normal, material, {aiTextureType_NORMAL_CAMERA, aiTextureType_NORMALS, aiTextureType_HEIGHT});
                AssignTexture(*mat, TextureType::Occlusion, material, {aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP});
                AssignTexture(*mat, TextureType::Emissive, material, {aiTextureType_EMISSION_COLOR, aiTextureType_EMISSIVE});

                ComputeMaterialData(*mat, material);

                if (!mat->passInfoAsset)
                    mat->passInfoAsset = ResourceManager::Get().Load<PassInfoAsset>(Path::RuntimeAssets + "PassInfo/standard_pbr.pass");
                mat->SyncParamsFromLegacy();

                mi.material = mat.get();
                materialByIndex[mesh->mMaterialIndex] = mat.get();
                m_materials.push_back(std::move(mat));
            }

            // geometry
            const aiVector3D *positions = mesh->mVertices;
            const aiVector3D *normals = mesh->mNormals;
            const aiVector3D *uvs = mesh->mTextureCoords[0];
            const aiColor4D *colors = mesh->mColors[0];

            std::vector<Vertex> meshVertices;
            std::vector<PositionUvVertex> meshPosUvs;
            meshVertices.reserve(mesh->mNumVertices);
            meshPosUvs.reserve(mesh->mNumVertices);

            // bounding box (local)
            bool bbInit = false;
            vec3 bbMin(0.f), bbMax(0.f);
            // Static vertices must bypass skinning in scenes that also contain animated meshes.
            const float defaultWeight = mesh->mNumBones > 0 ? 1.f : 0.f;

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

                if (mesh->mTangents)
                {
                    const aiVector3D &t = mesh->mTangents[v];
                    const aiVector3D &b = mesh->mBitangents[v];
                    const aiVector3D &n = normals ? normals[v] : aiVector3D(0.0f, 0.0f, 1.0f);
                    float det = (n.y * t.z - n.z * t.y) * b.x + (n.z * t.x - n.x * t.z) * b.y + (n.x * t.y - n.y * t.x) * b.z;
                    float sign = (det < 0.0f) ? -1.0f : 1.0f;
                    FillVertexTangent(vertex, t.x, t.y, t.z, sign);
                }
                else
                {
                    FillVertexTangent(vertex, 1.0f, 0.0f, 0.0f, 1.0f);
                }

                FillVertexJointsWeights(vertex, 0, 0, 0, 0, defaultWeight, 0.f, 0.f, 0.f);
                FillVertexJointsWeights(positionUvVertex, 0, 0, 0, 0, defaultWeight, 0.f, 0.f, 0.f);

                // Fill temp buffers
                meshVertices.push_back(vertex);
                meshPosUvs.push_back(positionUvVertex);
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
                    bbMin = min(bbMin, pos);
                    bbMax = max(bbMax, pos);
                }
            }

            if (bbInit)
            {
                mi.boundingBox.min = bbMin;
                mi.boundingBox.max = bbMax;
            }

            // Extract bone weights
            AnimationImporter::ExtractBoneWeights(mesh, m_skeleton, meshVertices, meshPosUvs);
            if (mesh->mNumBones > 0)
                mi.skinned = true;

            // Meshoptimizer
            std::vector<unsigned int> meshIndices;
            meshIndices.reserve(mesh->mNumFaces * 3);
            for (unsigned int f = 0; f < mesh->mNumFaces; f++)
            {
                const aiFace &face = mesh->mFaces[f];
                if (face.mNumIndices == 3)
                {
                    meshIndices.push_back(face.mIndices[0]);
                    meshIndices.push_back(face.mIndices[1]);
                    meshIndices.push_back(face.mIndices[2]);
                }
            }

            // Generate remap table
            std::vector<unsigned int> remap(meshIndices.size());
            size_t vertex_count = meshopt_generateVertexRemap(
                remap.data(),
                meshIndices.data(),
                meshIndices.size(),
                meshVertices.data(),
                meshVertices.size(),
                sizeof(Vertex));

            // Remap buffers
            std::vector<unsigned int> remappedIndices(meshIndices.size());
            std::vector<Vertex> remappedVertices(vertex_count);
            std::vector<PositionUvVertex> remappedPosUvs(vertex_count);

            meshopt_remapIndexBuffer(remappedIndices.data(), meshIndices.data(), meshIndices.size(), remap.data());
            meshopt_remapVertexBuffer(remappedVertices.data(), meshVertices.data(), meshVertices.size(), sizeof(Vertex), remap.data());
            meshopt_remapVertexBuffer(remappedPosUvs.data(), meshPosUvs.data(), meshPosUvs.size(), sizeof(PositionUvVertex), remap.data());

            // Optimize vertex cache
            meshopt_optimizeVertexCache(remappedIndices.data(), remappedIndices.data(), remappedIndices.size(), vertex_count);

            // Optimize overdraw
            meshopt_optimizeOverdraw(remappedIndices.data(), remappedIndices.data(), remappedIndices.size(), reinterpret_cast<float *>(remappedVertices.data()), vertex_count, sizeof(Vertex), 1.05f);

            // Optimize vertex fetch
            std::vector<unsigned int> fetchRemap(vertex_count);
            size_t unique_verts = meshopt_optimizeVertexFetchRemap(fetchRemap.data(), remappedIndices.data(), remappedIndices.size(), vertex_count);

            // Remap index buffer with the new fetch-optimized order
            meshopt_remapIndexBuffer(remappedIndices.data(), remappedIndices.data(), remappedIndices.size(), fetchRemap.data());

            // Remap both vertex buffers
            std::vector<Vertex> finalVertices(unique_verts);
            std::vector<PositionUvVertex> finalPosUvs(unique_verts);

            meshopt_remapVertexBuffer(finalVertices.data(), remappedVertices.data(), vertex_count, sizeof(Vertex), fetchRemap.data());
            meshopt_remapVertexBuffer(finalPosUvs.data(), remappedPosUvs.data(), vertex_count, sizeof(PositionUvVertex), fetchRemap.data());

            // Use the final unique (and reordered) buffers
            remappedVertices = std::move(finalVertices);
            remappedPosUvs = std::move(finalPosUvs);
            vertex_count = unique_verts;

            // Update MeshInfo with actual offsets and counts
            mi.vertexOffset = static_cast<uint32_t>(m_vertices.size());
            mi.verticesCount = static_cast<uint32_t>(vertex_count);
            mi.indexOffset = static_cast<uint32_t>(m_indices.size());
            mi.indicesCount = static_cast<uint32_t>(remappedIndices.size());
            mi.aabbVertexOffset = m_aabbVertices.size();

            // Push to global
            m_vertices.insert(m_vertices.end(), remappedVertices.begin(), remappedVertices.end());
            m_positionUvs.insert(m_positionUvs.end(), remappedPosUvs.begin(), remappedPosUvs.end());
            m_indices.insert(m_indices.end(), remappedIndices.begin(), remappedIndices.end());

            progress += mesh->mNumFaces;
            m_meshCount++;                   // used for aabb offset calculation only if we didn't track it manually
            m_verticesCount += vertex_count; // Global stats
            m_indicesCount += remappedIndices.size();

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

    void ModelAssetAssimp::SetupNodes()
    {
        auto &gSettings = Settings::Get<SceneSettings>();
        auto &progress = gSettings.loading.current;
        auto &total = gSettings.loading.total;

        gSettings.loading.SetName("Loading nodes");
        progress = 0;

        int nodeCount = 0;
        CountNodesRecursive(m_scene->mRootNode, nodeCount);
        total = nodeCount;

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

    void ModelAssetAssimp::ProcessNode(const aiNode *node, int parentIndex)
    {
        auto pushNode = [&](const aiNode *srcNode, int parent, const mat4 &local, int meshIdx) -> int
        {
            const std::string nodeName = (srcNode && srcNode->mName.length > 0) ? srcNode->mName.C_Str() : ("Node " + std::to_string(m_nodeInfos.size()));
            return CreateNode(nodeName, parent, local, meshIdx);
        };

        const mat4 local = AiToGlmMat4(node->mTransformation);

        // this node is the transform carrier
        const int transformNodeIdx = pushNode(node, parentIndex, local, -1);

        // The imported source node is the transform carrier.
        // Extra meshes stay under that node with identity local transform so all source meshes
        // keep moving together if the node is edited or reparented later.
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
                    pushNode(node, transformNodeIdx, mat4(1.f), meshIdx);
                }
            }
        }

        // children inherit transformNodeIdx
        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            ProcessNode(node->mChildren[i], transformNodeIdx);
    }

    bool ModelAssetAssimp::GetEmbeddedTextureBytes(const std::string &textureName,
                                                   std::vector<uint8_t> &outBytes,
                                                   std::string &outExtension) const
    {
        // Embedded textures are named "*N" (index into aiScene::mTextures). The Importer keeps the
        // aiScene alive for the model's lifetime, so the bytes are still available at cook time.
        if (!m_scene || textureName.empty() || textureName[0] != '*')
            return false;

        int index = -1;
        try
        {
            index = std::stoi(textureName.substr(1));
        }
        catch (...)
        {
            return false;
        }
        if (index < 0 || static_cast<unsigned int>(index) >= m_scene->mNumTextures)
            return false;

        const aiTexture *tex = m_scene->mTextures[index];
        if (!tex || !tex->pcData)
            return false;

        // mHeight == 0 => pcData is a compressed image FILE (PNG/JPG/...) of mWidth bytes. This is what
        // glTF/.glb embeds; write those bytes verbatim — exact fidelity, no re-encode.
        if (tex->mHeight == 0)
        {
            const uint8_t *data = reinterpret_cast<const uint8_t *>(tex->pcData);
            outBytes.assign(data, data + tex->mWidth);

            std::string hint = tex->achFormatHint;
            if (hint.empty())
                hint = "png";
            if (hint == "jpeg")
                hint = "jpg";
            outExtension = hint;
            return !outBytes.empty();
        }

        // Raw uncompressed BGRA (uncommon for glTF). We don't re-encode here, so defer to the default
        // texture on load rather than emit a headerless blob no loader can read.
        PE_WARN("[ModelAssetAssimp] Embedded texture %s is raw (uncompressed); skipping cook extraction",
                textureName.c_str());
        return false;
    }

    std::filesystem::path ModelAssetAssimp::GetTexturePath(const aiMaterial *material, aiTextureType type, int index) const
    {
        aiString path;
        if (material->GetTexture(type, index, &path) != AI_SUCCESS || path.length == 0)
            return {};

        // embedded textures like "*0" not handled here
        // embedded textures like "*0"
        if (path.C_Str()[0] == '*')
            return std::string(path.C_Str());

        // Use u8path because Assimp stores strings as UTF-8
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

            // Niagara Bistro references PNGs, but many packs ship DDS only.
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

        PE_WARN("[ModelAsset] Failed to find texture, using default: %s", pathStr.c_str());
        return {};
    }

    void ModelAssetAssimp::AssignTexture(Material &mat, TextureType type, aiMaterial *material,
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

    void ModelAssetAssimp::ComputeMaterialData(Material &mat, aiMaterial *material) const
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

    RenderType ModelAssetAssimp::DetermineRenderType(aiMaterial *material) const
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

        float opacity = 1.0f;
        if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS && opacity < 1.0f)
            return RenderType::AlphaBlend;

        return RenderType::Opaque;
    }

    void ModelAssetAssimp::ExtractAnimations()
    {
        if (!m_scene)
            return;

        AnimationImporter::ResolveBoneParents(m_scene, m_skeleton);
        AnimationImporter::ExtractAnimationClips(m_scene, m_skeleton, m_animations);
    }

} // namespace pe
