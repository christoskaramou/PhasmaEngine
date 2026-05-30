#include "Scene/ModelAssetCooked.h"
#include "Scene/ModelAsset.h"
#include "Scene/Material.h"
#include "Scene/PassInfoAsset.h"
#include "API/Image.h"
#include "API/Vertex.h"
#include "Base/FileSystem.h"

namespace pe
{
    namespace
    {
        // ".pemesh" binary layout. All fields little-endian; desktop (x86-64) and Android (arm64-v8a)
        // are both little-endian with identical IEEE-754 float and natural POD alignment, so the
        // vertex/index/aabb streams are a straight blit on both. The sizeof guards in the header catch
        // any future struct-layout change so a stale cooked file fails loudly instead of corrupting.
        constexpr char kMagic[4] = {'P', 'E', 'M', 'S'};
        constexpr uint32_t kVersion = 1;

        enum StreamFormat : uint32_t
        {
            Stream_PbrVertex = 0,   // Vertex            (gbuffer / PBR)
            Stream_ShadowPosUv = 1, // PositionUvVertex  (depth / shadows)
            Stream_AabbVertex = 2,  // AabbVertex        (debug bounds)
            Stream_IndexU32 = 3,    // uint32_t          (index buffer)
        };

#pragma pack(push, 1)
        struct Header
        {
            char magic[4];
            uint32_t version;
            uint32_t flags;
            uint32_t sizeofVertex;
            uint32_t sizeofPositionUv;
            uint32_t sizeofAabbVertex;
            uint32_t streamCount;
            uint32_t meshCount;
            uint32_t nodeCount;
            uint32_t materialCount;
        };

        struct StreamDesc
        {
            uint32_t formatId;
            uint32_t stride;
            uint32_t count;
            uint32_t byteLength;
        };

        struct MeshRecord
        {
            uint32_t vertexOffset;
            uint32_t verticesCount;
            uint32_t indexOffset;
            uint32_t indicesCount;
            uint32_t positionsOffset;
            uint64_t aabbVertexOffset;
            uint32_t aabbColor;
            float bbMin[3];
            float bbMax[3];
            int32_t renderType;
            uint8_t skinned;
            int32_t materialIndex;
        };
#pragma pack(pop)

        // --- byte buffer writer ---
        struct ByteWriter
        {
            std::vector<uint8_t> bytes;

            void Raw(const void *data, size_t size)
            {
                const uint8_t *p = static_cast<const uint8_t *>(data);
                bytes.insert(bytes.end(), p, p + size);
            }
            template <typename T>
            void Pod(const T &value) { Raw(&value, sizeof(T)); }
        };

        // --- byte buffer reader ---
        struct ByteReader
        {
            const uint8_t *data;
            size_t size;
            size_t cursor = 0;
            bool ok = true;

            bool Raw(void *dst, size_t n)
            {
                if (!ok || cursor + n > size)
                {
                    ok = false;
                    return false;
                }
                std::memcpy(dst, data + cursor, n);
                cursor += n;
                return true;
            }
            template <typename T>
            bool Pod(T &value) { return Raw(&value, sizeof(T)); }
            const uint8_t *View(size_t n)
            {
                if (!ok || cursor + n > size)
                {
                    ok = false;
                    return nullptr;
                }
                const uint8_t *p = data + cursor;
                cursor += n;
                return p;
            }
        };

        // Default white/normal material, mirroring Primitives::CreatePrimitiveModel so a cooked model
        // renders even before a .pescene overrides its textures. Geometry is the cooked payload;
        // material appearance still flows from the scene (texture paths + factors) at load.
        std::unique_ptr<Material> MakeDefaultMaterial()
        {
            auto &defaults = ModelAsset::GetDefaultResources();
            auto mat = std::make_unique<Material>();
            mat->name = "Cooked";
            mat->textures[static_cast<int>(TextureType::BaseColor)] = ResourceHandle<Image>::FromRaw(defaults.white);
            mat->textures[static_cast<int>(TextureType::Normal)] = ResourceHandle<Image>::FromRaw(defaults.normal);
            mat->textures[static_cast<int>(TextureType::MetallicRoughness)] = ResourceHandle<Image>::FromRaw(defaults.white);
            mat->textures[static_cast<int>(TextureType::Occlusion)] = ResourceHandle<Image>::FromRaw(defaults.white);
            mat->textures[static_cast<int>(TextureType::Emissive)] = ResourceHandle<Image>::FromRaw(defaults.black);
            for (auto &s : mat->samplers)
                s = defaults.sampler;
            mat->textureMask = 0;
            mat->metallic = 0.f;
            mat->roughness = 1.f;
            mat->occlusionStrength = 1.f;
            if (!mat->passInfoAsset)
                mat->passInfoAsset = ResourceManager::Get().Load<PassInfoAsset>(Path::Assets + "PassInfo/standard_pbr.pass");
            mat->SyncParamsFromLegacy();
            return mat;
        }
    } // namespace

    bool ModelAssetCooked::IsCookedPath(const std::filesystem::path &file)
    {
        std::string ext = file.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return ext == kExtension;
    }

    bool ModelAssetCooked::WriteToFile(const ModelAsset *model, const std::filesystem::path &file)
    {
        if (!model)
            return false;

        const std::vector<Vertex> &vertices = model->GetVertices();
        const std::vector<PositionUvVertex> &posUvs = model->GetPositionUvs();
        const std::vector<AabbVertex> &aabbs = model->GetAabbVertices();
        const std::vector<uint32_t> &indices = model->GetIndices();
        const std::vector<MeshInfo> &meshInfos = model->GetMeshInfos();
        const std::vector<NodeInfo> &nodeInfos = model->GetNodeInfos();
        const std::vector<std::unique_ptr<Material>> &materials = model->GetOwnedMaterials();

        // Map each owned material pointer to its index, so the per-mesh sharing structure survives.
        std::unordered_map<const Material *, int32_t> materialIndex;
        for (size_t i = 0; i < materials.size(); i++)
            materialIndex[materials[i].get()] = static_cast<int32_t>(i);

        ByteWriter w;

        Header header{};
        std::memcpy(header.magic, kMagic, 4);
        header.version = kVersion;
        header.flags = 0;
        header.sizeofVertex = static_cast<uint32_t>(sizeof(Vertex));
        header.sizeofPositionUv = static_cast<uint32_t>(sizeof(PositionUvVertex));
        header.sizeofAabbVertex = static_cast<uint32_t>(sizeof(AabbVertex));
        header.streamCount = 4;
        header.meshCount = static_cast<uint32_t>(meshInfos.size());
        header.nodeCount = static_cast<uint32_t>(nodeInfos.size());
        header.materialCount = static_cast<uint32_t>(materials.size());
        w.Pod(header);

        auto streamDesc = [](uint32_t fmt, size_t stride, size_t count)
        {
            StreamDesc d{};
            d.formatId = fmt;
            d.stride = static_cast<uint32_t>(stride);
            d.count = static_cast<uint32_t>(count);
            d.byteLength = static_cast<uint32_t>(stride * count);
            return d;
        };
        w.Pod(streamDesc(Stream_PbrVertex, sizeof(Vertex), vertices.size()));
        w.Pod(streamDesc(Stream_ShadowPosUv, sizeof(PositionUvVertex), posUvs.size()));
        w.Pod(streamDesc(Stream_AabbVertex, sizeof(AabbVertex), aabbs.size()));
        w.Pod(streamDesc(Stream_IndexU32, sizeof(uint32_t), indices.size()));

        // GPU-ready blobs, in stream-descriptor order.
        w.Raw(vertices.data(), vertices.size() * sizeof(Vertex));
        w.Raw(posUvs.data(), posUvs.size() * sizeof(PositionUvVertex));
        w.Raw(aabbs.data(), aabbs.size() * sizeof(AabbVertex));
        w.Raw(indices.data(), indices.size() * sizeof(uint32_t));

        // Mesh table.
        for (const MeshInfo &mi : meshInfos)
        {
            MeshRecord rec{};
            rec.vertexOffset = mi.vertexOffset;
            rec.verticesCount = mi.verticesCount;
            rec.indexOffset = mi.indexOffset;
            rec.indicesCount = mi.indicesCount;
            rec.positionsOffset = mi.positionsOffset;
            rec.aabbVertexOffset = static_cast<uint64_t>(mi.aabbVertexOffset);
            rec.aabbColor = mi.aabbColor;
            rec.bbMin[0] = mi.boundingBox.min.x;
            rec.bbMin[1] = mi.boundingBox.min.y;
            rec.bbMin[2] = mi.boundingBox.min.z;
            rec.bbMax[0] = mi.boundingBox.max.x;
            rec.bbMax[1] = mi.boundingBox.max.y;
            rec.bbMax[2] = mi.boundingBox.max.z;
            rec.renderType = static_cast<int32_t>(mi.renderType);
            rec.skinned = mi.skinned ? 1 : 0;
            auto it = materialIndex.find(mi.material);
            rec.materialIndex = it != materialIndex.end() ? it->second : -1;
            w.Pod(rec);
        }

        // Node table.
        for (int i = 0; i < static_cast<int>(nodeInfos.size()); i++)
        {
            const NodeInfo &ni = nodeInfos[i];
            int32_t parent = ni.parent;
            int32_t nodeToMesh = model->GetNodeMesh(i);
            w.Pod(parent);
            w.Raw(&ni.localMatrix, sizeof(float) * 16);
            w.Pod(nodeToMesh);
            uint32_t nameLen = static_cast<uint32_t>(ni.name.size());
            w.Pod(nameLen);
            if (nameLen)
                w.Raw(ni.name.data(), nameLen);
        }

        std::filesystem::path path(file);
        if (!path.parent_path().empty() && !std::filesystem::exists(path.parent_path()))
            std::filesystem::create_directories(path.parent_path());

        auto pathU8 = path.u8string();
        std::string pathStr(reinterpret_cast<const char *>(pathU8.c_str()));
        FileSystem out(pathStr, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!out.IsOpen())
        {
            PE_WARN("[ModelAssetCooked] Failed to open for write: %s", pathStr.c_str());
            return false;
        }
        out.Write(reinterpret_cast<const char *>(w.bytes.data()), w.bytes.size());
        PE_INFO("[ModelAssetCooked] Cooked %u meshes, %zu verts, %zu indices -> %s (%zu bytes)",
                header.meshCount, vertices.size(), indices.size(), pathStr.c_str(), w.bytes.size());
        return true;
    }

    ModelAsset *ModelAssetCooked::Load(const std::filesystem::path &file)
    {
        if (!std::filesystem::exists(file))
        {
            PE_WARN("[ModelAssetCooked] File not found: %s", file.generic_string().c_str());
            return nullptr;
        }

        auto pathU8 = file.u8string();
        std::string pathStr(reinterpret_cast<const char *>(pathU8.c_str()));
        FileSystem in(pathStr);
        if (!in.IsOpen())
        {
            PE_WARN("[ModelAssetCooked] Failed to open: %s", pathStr.c_str());
            return nullptr;
        }
        std::vector<uint8_t> blob = in.ReadAllBytes();

        ByteReader r{blob.data(), blob.size()};
        Header header{};
        if (!r.Pod(header))
        {
            PE_WARN("[ModelAssetCooked] Truncated header: %s", pathStr.c_str());
            return nullptr;
        }
        if (std::memcmp(header.magic, kMagic, 4) != 0 || header.version != kVersion)
        {
            PE_WARN("[ModelAssetCooked] Bad magic/version (got v%u) for %s", header.version, pathStr.c_str());
            return nullptr;
        }
        if (header.sizeofVertex != sizeof(Vertex) ||
            header.sizeofPositionUv != sizeof(PositionUvVertex) ||
            header.sizeofAabbVertex != sizeof(AabbVertex))
        {
            PE_WARN("[ModelAssetCooked] Vertex ABI mismatch for %s (rebuild/re-cook required)", pathStr.c_str());
            return nullptr;
        }

        std::vector<StreamDesc> streams(header.streamCount);
        for (uint32_t i = 0; i < header.streamCount; i++)
            if (!r.Pod(streams[i]))
            {
                PE_WARN("[ModelAssetCooked] Truncated stream table: %s", pathStr.c_str());
                return nullptr;
            }

        ModelAsset *model = new ModelAsset();

        // Stream blobs, in descriptor order.
        for (const StreamDesc &s : streams)
        {
            const uint8_t *p = r.View(s.byteLength);
            if (!p)
            {
                PE_WARN("[ModelAssetCooked] Truncated stream blob: %s", pathStr.c_str());
                delete model;
                return nullptr;
            }
            switch (s.formatId)
            {
            case Stream_PbrVertex:
                model->m_vertices.resize(s.count);
                if (s.count)
                    std::memcpy(model->m_vertices.data(), p, s.byteLength);
                break;
            case Stream_ShadowPosUv:
                model->m_positionUvs.resize(s.count);
                if (s.count)
                    std::memcpy(model->m_positionUvs.data(), p, s.byteLength);
                break;
            case Stream_AabbVertex:
                model->m_aabbVertices.resize(s.count);
                if (s.count)
                    std::memcpy(model->m_aabbVertices.data(), p, s.byteLength);
                break;
            case Stream_IndexU32:
                model->m_indices.resize(s.count);
                if (s.count)
                    std::memcpy(model->m_indices.data(), p, s.byteLength);
                break;
            default:
                break; // unknown stream: skip (already advanced cursor)
            }
        }

        // Materials: rebuild the sharing structure with default materials. A .pescene overlay will
        // override textures/factors; an imported (non-scene) model renders with the default look.
        model->m_materials.clear();
        model->m_materials.reserve(header.materialCount);
        for (uint32_t i = 0; i < header.materialCount; i++)
            model->m_materials.push_back(MakeDefaultMaterial());

        // Mesh table.
        model->m_meshInfos.resize(header.meshCount);
        for (uint32_t i = 0; i < header.meshCount; i++)
        {
            MeshRecord rec{};
            if (!r.Pod(rec))
            {
                PE_WARN("[ModelAssetCooked] Truncated mesh table: %s", pathStr.c_str());
                delete model;
                return nullptr;
            }
            MeshInfo &mi = model->m_meshInfos[i];
            mi.vertexOffset = rec.vertexOffset;
            mi.verticesCount = rec.verticesCount;
            mi.indexOffset = rec.indexOffset;
            mi.indicesCount = rec.indicesCount;
            mi.positionsOffset = rec.positionsOffset;
            mi.aabbVertexOffset = static_cast<size_t>(rec.aabbVertexOffset);
            mi.aabbColor = rec.aabbColor;
            mi.boundingBox.min = vec3(rec.bbMin[0], rec.bbMin[1], rec.bbMin[2]);
            mi.boundingBox.max = vec3(rec.bbMax[0], rec.bbMax[1], rec.bbMax[2]);
            mi.renderType = static_cast<RenderType>(rec.renderType);
            mi.skinned = rec.skinned != 0;
            if (rec.materialIndex >= 0 && rec.materialIndex < static_cast<int32_t>(model->m_materials.size()))
                mi.material = model->m_materials[rec.materialIndex].get();
            else
                mi.material = nullptr;
        }

        // Node table (parents resolved in a second pass; children derived from parents).
        std::vector<int32_t> parents(header.nodeCount, -1);
        for (uint32_t i = 0; i < header.nodeCount; i++)
        {
            int32_t parent = -1, nodeToMesh = -1;
            mat4 localMatrix(1.f);
            if (!r.Pod(parent) || !r.Raw(&localMatrix, sizeof(float) * 16) || !r.Pod(nodeToMesh))
            {
                PE_WARN("[ModelAssetCooked] Truncated node table: %s", pathStr.c_str());
                delete model;
                return nullptr;
            }
            uint32_t nameLen = 0;
            if (!r.Pod(nameLen))
            {
                delete model;
                return nullptr;
            }
            std::string name;
            if (nameLen)
            {
                const uint8_t *np = r.View(nameLen);
                if (!np)
                {
                    delete model;
                    return nullptr;
                }
                name.assign(reinterpret_cast<const char *>(np), nameLen);
            }
            parents[i] = parent;
            model->CreateNode(name, -1, localMatrix, nodeToMesh);
        }
        for (uint32_t i = 0; i < header.nodeCount; i++)
            model->SetNodeParentIndex(static_cast<int>(i), parents[i]);
        model->RebuildNodeChildrenFromParents();

        model->m_verticesCount = static_cast<uint32_t>(model->m_vertices.size());
        model->m_indicesCount = static_cast<uint32_t>(model->m_indices.size());
        model->m_meshCount = header.meshCount;

        auto labelU8 = file.stem().u8string();
        model->SetLabel(std::string(reinterpret_cast<const char *>(labelU8.c_str())));

        PE_INFO("[ModelAssetCooked] Loaded %s: %u meshes, %zu verts, %zu indices",
                pathStr.c_str(), header.meshCount, model->m_vertices.size(), model->m_indices.size());
        return model;
    }
} // namespace pe
