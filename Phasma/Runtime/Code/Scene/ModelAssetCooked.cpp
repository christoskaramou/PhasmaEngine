#include "Scene/ModelAssetCooked.h"
#include "Scene/ModelAsset.h"
#include "Scene/Material.h"
#include "Scene/PassInfoAsset.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Vertex.h"

namespace pe
{
    namespace
    {
        // ".pemesh" binary layout. All fields little-endian; desktop (x86-64) and Android (arm64-v8a)
        // are both little-endian with identical IEEE-754 float and natural POD alignment, so the
        // vertex/index/aabb streams are a straight blit on both. The sizeof guards in the header catch
        // any future struct-layout change so a stale cooked file fails loudly instead of corrupting.
        constexpr char kMagic[4] = {'P', 'E', 'M', 'S'};
        constexpr uint32_t kVersion = 3;
        constexpr int kTextureSlotCount = 5;

        static_assert(static_cast<int>(TextureType::Emissive) + 1 == kTextureSlotCount);

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
            uint32_t boneCount;
            uint32_t clipCount;
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

        struct MaterialScalarRecord
        {
            float baseColorFactor[4];
            float emissiveFactor[3];
            float metallic;
            float roughness;
            float alphaCutoff;
            float occlusionStrength;
            float normalScale;
            float transmissionFactor;
            float thicknessFactor;
            float attenuationDistance;
            float ior;
            float attenuationColor[3];
            int32_t renderType;
            uint8_t doubleSided;
            uint32_t textureMask;
        };
#pragma pack(pop)

        static_assert(sizeof(MaterialScalarRecord) == 85);

        // Animation keyframe arrays are raw-blitted; guard that memcpy is valid. The glm aligned
        // layout is identical on x86-64 and arm64 (same as the vertex streams), so the blit is portable.
        static_assert(std::is_trivially_copyable_v<PositionKey>);
        static_assert(std::is_trivially_copyable_v<RotationKey>);
        static_assert(std::is_trivially_copyable_v<ScaleKey>);

        struct CookedMaterialRecord
        {
            std::string name;
            MaterialScalarRecord scalars{};
            std::array<std::string, kTextureSlotCount> texturePaths{};
        };

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
            void String(const std::string &value)
            {
                uint32_t len = static_cast<uint32_t>(value.size());
                Pod(len);
                if (len)
                    Raw(value.data(), len);
            }
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
            bool String(std::string &value)
            {
                uint32_t len = 0;
                if (!Pod(len))
                    return false;

                value.clear();
                if (len == 0)
                    return true;

                const uint8_t *p = View(len);
                if (!p)
                    return false;
                value.assign(reinterpret_cast<const char *>(p), len);
                return true;
            }
        };

        template <typename KeyT>
        void WriteKeys(ByteWriter &w, const std::vector<KeyT> &keys)
        {
            uint32_t count = static_cast<uint32_t>(keys.size());
            w.Pod(count);
            if (count)
                w.Raw(keys.data(), keys.size() * sizeof(KeyT));
        }

        template <typename KeyT>
        bool ReadKeys(ByteReader &r, std::vector<KeyT> &keys)
        {
            uint32_t count = 0;
            if (!r.Pod(count))
                return false;
            keys.resize(count);
            if (count)
                return r.Raw(keys.data(), static_cast<size_t>(count) * sizeof(KeyT));
            return true;
        }

        std::string PathToUtf8String(const std::filesystem::path &path)
        {
            auto pathU8 = path.u8string();
            return std::string(reinterpret_cast<const char *>(pathU8.c_str()), pathU8.size());
        }

        std::filesystem::path PathFromUtf8String(const std::string &path)
        {
            return std::filesystem::path(reinterpret_cast<const char8_t *>(path.c_str()));
        }

        uint32_t TextureMaskBit(int slot)
        {
            return 1u << static_cast<uint32_t>(slot);
        }

        bool IsPortableRelativePath(const std::filesystem::path &path)
        {
            if (path.empty() || path.is_absolute())
                return false;

            const std::filesystem::path normalized = path.lexically_normal();
            auto it = normalized.begin();
            return it != normalized.end() && *it != "..";
        }

        bool IsEmbeddedTextureName(const std::string &name)
        {
            return name.empty() || name[0] == '*' || name.rfind("Embedded_", 0) == 0;
        }

        std::filesystem::path NormalizeExistingPath(const std::filesystem::path &path)
        {
            std::error_code ec;
            std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
            if (!ec)
                return normalized;

            ec.clear();
            normalized = std::filesystem::absolute(path, ec);
            if (!ec)
                return normalized.lexically_normal();

            return path.lexically_normal();
        }

        std::filesystem::path MakeCookedTextureRelativePath(const std::filesystem::path &texturePath,
                                                            const std::filesystem::path &sourceDir)
        {
            std::error_code ec;
            std::filesystem::path relativePath;
            if (!sourceDir.empty())
                relativePath = std::filesystem::relative(texturePath, sourceDir, ec);

            if (ec || !IsPortableRelativePath(relativePath))
                relativePath = texturePath.filename();

            return relativePath.lexically_normal();
        }

        bool CopyCookedTexture(const std::filesystem::path &sourcePath,
                               const std::filesystem::path &outputDir,
                               const std::filesystem::path &relativePath)
        {
            if (sourcePath.empty() || relativePath.empty())
                return true;

            const std::filesystem::path dstPath = outputDir / relativePath;

            std::error_code ec;
            std::filesystem::create_directories(dstPath.parent_path(), ec);
            if (ec)
            {
                PE_WARN("[ModelAssetCooked] Failed to create texture directory '%s': %s",
                        PathToUtf8String(dstPath.parent_path()).c_str(), ec.message().c_str());
                return false;
            }

            ec.clear();
            if (std::filesystem::exists(dstPath, ec))
            {
                ec.clear();
                if (std::filesystem::equivalent(sourcePath, dstPath, ec) && !ec)
                    return true;
            }

            ec.clear();
            std::filesystem::copy_file(sourcePath, dstPath, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec)
            {
                PE_WARN("[ModelAssetCooked] Failed to copy texture '%s' -> '%s': %s",
                        PathToUtf8String(sourcePath).c_str(), PathToUtf8String(dstPath).c_str(), ec.message().c_str());
                return false;
            }

            return true;
        }

        // Embedded textures (.glb) have no source file; recover the original encoded bytes from the
        // model and write them next to the .pemesh so the slot resolves like any external texture.
        std::string SerializeEmbeddedTexture(const ModelAsset &model,
                                             const std::string &imageName,
                                             const std::filesystem::path &outputDir,
                                             const std::string &stem,
                                             std::unordered_map<std::string, std::string> &written)
        {
            // An embedded texture shared across slots/materials is written once.
            auto cached = written.find(imageName);
            if (cached != written.end())
                return cached->second;

            std::vector<uint8_t> bytes;
            std::string ext;
            if (!model.GetEmbeddedTextureBytes(imageName, bytes, ext) || bytes.empty())
            {
                written[imageName] = {}; // remember the miss; load falls back to the default texture
                return {};
            }

            // Deterministic sibling file: <stem>_embedded<N>.<ext>  (imageName is "*N").
            std::string suffix = imageName;
            suffix.erase(0, 1);
            const std::filesystem::path relPath = stem + "_embedded" + suffix + "." + ext;
            const std::filesystem::path dstPath = outputDir / relPath;

            std::error_code ec;
            std::filesystem::create_directories(dstPath.parent_path(), ec);

            FileSystem out(PathToUtf8String(dstPath), std::ios::out | std::ios::trunc | std::ios::binary);
            if (!out.IsOpen())
            {
                PE_WARN("[ModelAssetCooked] Failed to write embedded texture '%s'",
                        PathToUtf8String(dstPath).c_str());
                written[imageName] = {};
                return {};
            }
            out.Write(reinterpret_cast<const char *>(bytes.data()), bytes.size());

            const std::string rel = relPath.generic_string();
            written[imageName] = rel;
            return rel;
        }

        std::string SerializeTexturePath(const Material &material,
                                         int slot,
                                         const std::filesystem::path &sourceDir,
                                         const std::filesystem::path &outputDir,
                                         const ModelAsset &model,
                                         const std::string &stem,
                                         std::unordered_map<std::string, std::string> &embeddedWritten,
                                         bool &ok)
        {
            if ((material.textureMask & TextureMaskBit(slot)) == 0)
                return {};

            Image *image = material.textures[slot].get();
            if (!image)
                return {};

            const std::string &imageName = image->GetName();
            if (IsEmbeddedTextureName(imageName))
            {
                // Embedded textures carry their aiScene index in the resource id ("*N"); the display
                // name is empty. Use the id so the model can recover the original encoded bytes.
                return SerializeEmbeddedTexture(model, image->GetResourceId(), outputDir, stem, embeddedWritten);
            }

            std::filesystem::path texturePath = NormalizeExistingPath(PathFromUtf8String(imageName));
            std::error_code ec;
            if (!std::filesystem::is_regular_file(texturePath, ec))
            {
                PE_WARN("[ModelAssetCooked] Texture path is not a file, deferring slot %d: %s",
                        slot, imageName.c_str());
                return {};
            }

            const std::filesystem::path relativePath = MakeCookedTextureRelativePath(texturePath, sourceDir);
            if (relativePath.empty())
                return {};

            ok = CopyCookedTexture(texturePath, outputDir, relativePath);
            if (!ok)
                return {};

            return relativePath.generic_string();
        }

        MaterialScalarRecord MakeScalarRecord(const Material &material)
        {
            MaterialScalarRecord rec{};
            rec.baseColorFactor[0] = material.baseColorFactor.r;
            rec.baseColorFactor[1] = material.baseColorFactor.g;
            rec.baseColorFactor[2] = material.baseColorFactor.b;
            rec.baseColorFactor[3] = material.baseColorFactor.a;
            rec.emissiveFactor[0] = material.emissiveFactor.r;
            rec.emissiveFactor[1] = material.emissiveFactor.g;
            rec.emissiveFactor[2] = material.emissiveFactor.b;
            rec.metallic = material.metallic;
            rec.roughness = material.roughness;
            rec.alphaCutoff = material.alphaCutoff;
            rec.occlusionStrength = material.occlusionStrength;
            rec.normalScale = material.normalScale;
            rec.transmissionFactor = material.transmissionFactor;
            rec.thicknessFactor = material.thicknessFactor;
            rec.attenuationDistance = material.attenuationDistance;
            rec.ior = material.ior;
            rec.attenuationColor[0] = material.attenuationColor.r;
            rec.attenuationColor[1] = material.attenuationColor.g;
            rec.attenuationColor[2] = material.attenuationColor.b;
            rec.renderType = static_cast<int32_t>(material.renderType);
            rec.doubleSided = material.doubleSided ? 1 : 0;
            rec.textureMask = material.textureMask;
            return rec;
        }

        void ApplyScalarRecord(Material &material, const MaterialScalarRecord &rec)
        {
            material.baseColorFactor = vec4(rec.baseColorFactor[0], rec.baseColorFactor[1], rec.baseColorFactor[2], rec.baseColorFactor[3]);
            material.emissiveFactor = vec3(rec.emissiveFactor[0], rec.emissiveFactor[1], rec.emissiveFactor[2]);
            material.metallic = rec.metallic;
            material.roughness = rec.roughness;
            material.alphaCutoff = rec.alphaCutoff;
            material.occlusionStrength = rec.occlusionStrength;
            material.normalScale = rec.normalScale;
            material.transmissionFactor = rec.transmissionFactor;
            material.thicknessFactor = rec.thicknessFactor;
            material.attenuationDistance = rec.attenuationDistance;
            material.ior = rec.ior;
            material.attenuationColor = vec3(rec.attenuationColor[0], rec.attenuationColor[1], rec.attenuationColor[2]);
            material.renderType = static_cast<RenderType>(rec.renderType);
            material.doubleSided = rec.doubleSided != 0;
            material.textureMask = rec.textureMask;
        }

        // Default white/normal material, mirroring Primitives::CreatePrimitiveModel so a cooked model
        // has valid shader inputs even when a serialized texture is missing or deferred.
        std::unique_ptr<Material> MakeDefaultMaterial(CommandBuffer *cmd)
        {
            auto &defaults = ModelAsset::GetDefaultResources(cmd);
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
                mat->passInfoAsset = ResourceManager::Get().Load<PassInfoAsset>(Path::RuntimeAssets + "PassInfo/standard_pbr.pass");
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

        std::filesystem::path path(file);
        const std::filesystem::path outputDir = path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();
        std::error_code ec;
        std::filesystem::create_directories(outputDir, ec);
        if (ec)
        {
            PE_WARN("[ModelAssetCooked] Failed to create output directory '%s': %s",
                    PathToUtf8String(outputDir).c_str(), ec.message().c_str());
            return false;
        }

        const std::vector<Vertex> &vertices = model->GetVertices();
        const std::vector<PositionUvVertex> &posUvs = model->GetPositionUvs();
        const std::vector<AabbVertex> &aabbs = model->GetAabbVertices();
        const std::vector<uint32_t> &indices = model->GetIndices();
        const std::vector<MeshInfo> &meshInfos = model->GetMeshInfos();
        const std::vector<NodeInfo> &nodeInfos = model->GetNodeInfos();
        const std::vector<std::unique_ptr<Material>> &materials = model->GetOwnedMaterials();
        const Skeleton &skeleton = model->GetSkeleton();
        const std::vector<AnimationClip> &animations = model->GetAnimations();
        const std::filesystem::path sourceDir = model->GetFilePath().empty()
                                                    ? std::filesystem::path()
                                                    : NormalizeExistingPath(model->GetFilePath().parent_path());

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
        header.boneCount = static_cast<uint32_t>(skeleton.bones.size());
        header.clipCount = static_cast<uint32_t>(animations.size());
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
            w.String(ni.name);
        }

        // Material table. MeshRecord::materialIndex points into this table.
        const std::string stem = PathToUtf8String(path.stem());
        std::unordered_map<std::string, std::string> embeddedWritten; // dedup embedded-texture writes
        for (const auto &materialPtr : materials)
        {
            Material defaultMaterial;
            const Material &material = materialPtr ? *materialPtr : defaultMaterial;
            w.String(material.name);
            w.Pod(MakeScalarRecord(material));

            for (int slot = 0; slot < kTextureSlotCount; slot++)
            {
                bool copyOk = true;
                const std::string relPath =
                    SerializeTexturePath(material, slot, sourceDir, outputDir, *model, stem, embeddedWritten, copyOk);
                if (!copyOk)
                    return false;
                w.String(relPath);
            }
        }

        // Skeleton table (v3): rootTransform + bones. boneNameToIndex is rebuilt from names on load.
        if (header.boneCount > 0)
        {
            w.Raw(&skeleton.rootTransform, sizeof(float) * 16);
            for (const BoneInfo &bone : skeleton.bones)
            {
                w.String(bone.name);
                int32_t parentIndex = bone.parentIndex;
                w.Pod(parentIndex);
                w.Raw(&bone.offsetMatrix, sizeof(float) * 16);
                w.Raw(&bone.localBindTransform, sizeof(float) * 16);
                w.Raw(&bone.intermediatePrefix, sizeof(float) * 16);
            }
        }

        // Animation table (v3): one clip per entry, each with per-bone TRS keyframe channels. Vertex
        // joint indices/weights are already in the cooked PBR vertex stream.
        for (const AnimationClip &clip : animations)
        {
            w.String(clip.name);
            w.Pod(clip.duration);
            w.Pod(clip.ticksPerSecond);
            uint32_t channelCount = static_cast<uint32_t>(clip.channels.size());
            w.Pod(channelCount);
            for (const AnimationChannel &channel : clip.channels)
            {
                int32_t boneIndex = channel.boneIndex;
                w.Pod(boneIndex);
                WriteKeys(w, channel.positionKeys);
                WriteKeys(w, channel.rotationKeys);
                WriteKeys(w, channel.scaleKeys);
            }
        }

        auto pathU8 = path.u8string();
        std::string pathStr(reinterpret_cast<const char *>(pathU8.c_str()));
        FileSystem out(pathStr, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!out.IsOpen())
        {
            PE_WARN("[ModelAssetCooked] Failed to open for write: %s", pathStr.c_str());
            return false;
        }
        out.Write(reinterpret_cast<const char *>(w.bytes.data()), w.bytes.size());
        PE_INFO("[ModelAssetCooked] Cooked %u meshes, %zu verts, %zu indices, %u materials, %u bones, %u clips -> %s (%zu bytes)",
                header.meshCount, vertices.size(), indices.size(), header.materialCount, header.boneCount,
                header.clipCount, pathStr.c_str(), w.bytes.size());
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
        if (std::memcmp(header.magic, kMagic, 4) != 0)
        {
            PE_WARN("[ModelAssetCooked] Bad magic for %s", pathStr.c_str());
            return nullptr;
        }
        if (header.version != kVersion)
        {
            PE_WARN("[ModelAssetCooked] Unsupported version (got v%u, expected v%u) for %s; re-cook required",
                    header.version, kVersion, pathStr.c_str());
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

        // Mesh table.
        std::vector<int32_t> meshMaterialIndices(header.meshCount, -1);
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
            mi.material = nullptr;
            meshMaterialIndices[i] = rec.materialIndex;
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
            std::string name;
            if (!r.String(name))
            {
                PE_WARN("[ModelAssetCooked] Truncated node name table: %s", pathStr.c_str());
                delete model;
                return nullptr;
            }
            parents[i] = parent;
            model->CreateNode(name, -1, localMatrix, nodeToMesh);
        }

        std::vector<CookedMaterialRecord> materialRecords;
        materialRecords.resize(header.materialCount);
        for (uint32_t i = 0; i < header.materialCount; i++)
        {
            CookedMaterialRecord &record = materialRecords[i];
            if (!r.String(record.name) || !r.Pod(record.scalars))
            {
                PE_WARN("[ModelAssetCooked] Truncated material table: %s", pathStr.c_str());
                delete model;
                return nullptr;
            }

            for (int slot = 0; slot < kTextureSlotCount; slot++)
                if (!r.String(record.texturePaths[slot]))
                {
                    PE_WARN("[ModelAssetCooked] Truncated material texture table: %s", pathStr.c_str());
                    delete model;
                    return nullptr;
                }
        }

        // Skeleton (v3): rebuild m_skeleton; boneNameToIndex is derived from bone names.
        if (header.boneCount > 0)
        {
            if (!r.Raw(&model->m_skeleton.rootTransform, sizeof(float) * 16))
            {
                PE_WARN("[ModelAssetCooked] Truncated skeleton header: %s", pathStr.c_str());
                delete model;
                return nullptr;
            }
            model->m_skeleton.bones.resize(header.boneCount);
            model->m_skeleton.boneNameToIndex.clear();
            for (uint32_t i = 0; i < header.boneCount; i++)
            {
                BoneInfo &bone = model->m_skeleton.bones[i];
                int32_t parentIndex = -1;
                if (!r.String(bone.name) || !r.Pod(parentIndex) ||
                    !r.Raw(&bone.offsetMatrix, sizeof(float) * 16) ||
                    !r.Raw(&bone.localBindTransform, sizeof(float) * 16) ||
                    !r.Raw(&bone.intermediatePrefix, sizeof(float) * 16))
                {
                    PE_WARN("[ModelAssetCooked] Truncated bone table: %s", pathStr.c_str());
                    delete model;
                    return nullptr;
                }
                bone.parentIndex = parentIndex;
                model->m_skeleton.boneNameToIndex[bone.name] = static_cast<int>(i);
            }
        }

        // Animation table (v3): clips with per-bone TRS keyframe channels.
        model->m_animations.resize(header.clipCount);
        for (uint32_t i = 0; i < header.clipCount; i++)
        {
            AnimationClip &clip = model->m_animations[i];
            uint32_t channelCount = 0;
            if (!r.String(clip.name) || !r.Pod(clip.duration) || !r.Pod(clip.ticksPerSecond) || !r.Pod(channelCount))
            {
                PE_WARN("[ModelAssetCooked] Truncated animation clip header: %s", pathStr.c_str());
                delete model;
                return nullptr;
            }
            clip.channels.resize(channelCount);
            for (uint32_t c = 0; c < channelCount; c++)
            {
                AnimationChannel &channel = clip.channels[c];
                int32_t boneIndex = -1;
                if (!r.Pod(boneIndex) ||
                    !ReadKeys(r, channel.positionKeys) ||
                    !ReadKeys(r, channel.rotationKeys) ||
                    !ReadKeys(r, channel.scaleKeys))
                {
                    PE_WARN("[ModelAssetCooked] Truncated animation channel: %s", pathStr.c_str());
                    delete model;
                    return nullptr;
                }
                channel.boneIndex = boneIndex;
            }
        }

        Queue *queue = RHII.GetMainQueue();
        if (!queue)
        {
            PE_WARN("[ModelAssetCooked] RHI main queue unavailable while loading materials: %s", pathStr.c_str());
            delete model;
            return nullptr;
        }

        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();

        model->ResetResources(cmd);
        model->m_materials.clear();
        model->m_materials.reserve(header.materialCount);

        std::unordered_map<std::string, ResourceHandle<Image>> loadedTextures;
        for (const CookedMaterialRecord &record : materialRecords)
        {
            auto material = MakeDefaultMaterial(cmd);
            material->name = record.name.empty() ? "Cooked" : record.name;
            ApplyScalarRecord(*material, record.scalars);

            for (int slot = 0; slot < kTextureSlotCount; slot++)
            {
                const std::string &relPathString = record.texturePaths[slot];
                if (relPathString.empty())
                    continue;

                const std::filesystem::path relPath = PathFromUtf8String(relPathString);
                if (!IsPortableRelativePath(relPath))
                {
                    PE_WARN("[ModelAssetCooked] Ignoring non-relative texture path for material '%s': %s",
                            material->name.c_str(), relPathString.c_str());
                    continue;
                }

                const std::filesystem::path texturePath = (file.parent_path() / relPath).lexically_normal();
                std::error_code ec;
                if (!std::filesystem::exists(texturePath, ec))
                {
                    PE_WARN("[ModelAssetCooked] Missing texture for material '%s': %s",
                            material->name.c_str(), PathToUtf8String(texturePath).c_str());
                    continue;
                }

                const std::filesystem::path textureKeyPath = NormalizeExistingPath(texturePath);
                const std::string textureKey = PathToUtf8String(textureKeyPath);

                ResourceHandle<Image> image;
                auto it = loadedTextures.find(textureKey);
                if (it != loadedTextures.end())
                {
                    image = it->second;
                }
                else
                {
                    image = model->LoadTexture(cmd, textureKeyPath);
                    loadedTextures[textureKey] = image;
                }

                if (image)
                    material->textures[slot] = image;
            }

            if (!material->passInfoAsset)
                material->passInfoAsset = ResourceManager::Get().Load<PassInfoAsset>(Path::RuntimeAssets + "PassInfo/standard_pbr.pass");
            material->SyncParamsFromLegacy();
            model->m_materials.push_back(std::move(material));
        }

        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();

        for (uint32_t i = 0; i < header.meshCount; i++)
        {
            const int32_t materialIndex = meshMaterialIndices[i];
            if (materialIndex >= 0 && materialIndex < static_cast<int32_t>(model->m_materials.size()))
                model->m_meshInfos[i].material = model->m_materials[materialIndex].get();
        }

        for (uint32_t i = 0; i < header.nodeCount; i++)
            model->SetNodeParentIndex(static_cast<int>(i), parents[i]);
        model->RebuildNodeChildrenFromParents();

        model->m_verticesCount = static_cast<uint32_t>(model->m_vertices.size());
        model->m_indicesCount = static_cast<uint32_t>(model->m_indices.size());
        model->m_meshCount = header.meshCount;

        auto labelU8 = file.stem().u8string();
        model->SetLabel(std::string(reinterpret_cast<const char *>(labelU8.c_str())));
        model->m_filePath = file;

        PE_INFO("[ModelAssetCooked] Loaded %s: %u meshes, %zu verts, %zu indices",
                pathStr.c_str(), header.meshCount, model->m_vertices.size(), model->m_indices.size());
        return model;
    }
} // namespace pe
