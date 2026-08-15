#include "Scene/Scene.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Scene/Material.h"
#include "Scene/ModelAsset.h"
#include "Scene/PassInfoAsset.h"
#include "Scene/Primitives.h"
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"

namespace pe
{
    namespace
    {
        std::filesystem::path U8Path(const std::string &utf8)
        {
            return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
        }

        std::string PathUtf8(const std::filesystem::path &path)
        {
            const auto u8 = path.u8string();
            return std::string(reinterpret_cast<const char *>(u8.c_str()), u8.size());
        }

        std::filesystem::path ExistingPath(std::initializer_list<std::filesystem::path> candidates)
        {
            for (const auto &candidate : candidates)
            {
                if (!candidate.empty() && AssetFileExists(candidate))
                    return candidate.lexically_normal();
            }
            return candidates.size() ? candidates.begin()->lexically_normal() : std::filesystem::path();
        }

        std::filesystem::path ResolveSpritePath(const std::string &path)
        {
            if (path.empty())
                return {};

            std::filesystem::path p = U8Path(path);
            if (p.is_absolute())
                return p.lexically_normal();

            const std::filesystem::path assets = U8Path(Path::Assets);
            const std::filesystem::path root = U8Path(Path::Root);
            return ExistingPath({p, assets / p, root / p});
        }

        std::filesystem::path ResolveMetadataRelativePath(const std::filesystem::path &metadataPath, const std::string &path)
        {
            if (path.empty())
                return {};

            std::filesystem::path p = U8Path(path);
            if (p.is_absolute())
                return p.lexically_normal();

            const std::filesystem::path assets = U8Path(Path::Assets);
            return ExistingPath({metadataPath.parent_path() / p, assets / p, p});
        }

        int ReadInt(const rapidjson::Value &value, int fallback)
        {
            return value.IsInt() ? value.GetInt() : fallback;
        }

        float ReadFloat(const rapidjson::Value &value, float fallback)
        {
            if (!value.IsNumber())
                return fallback;
            const float result = value.GetFloat();
            return std::isfinite(result) ? result : fallback;
        }

        bool ReadRect(const rapidjson::Value &value, int &x, int &y, int &w, int &h)
        {
            if (!value.IsArray() || value.Size() < 4)
                return false;
            if (!value[0].IsInt() || !value[1].IsInt() || !value[2].IsInt() || !value[3].IsInt())
                return false;
            x = std::max(0, value[0].GetInt());
            y = std::max(0, value[1].GetInt());
            w = std::max(1, value[2].GetInt());
            h = std::max(1, value[3].GetInt());
            return true;
        }

        vec4 FrameUvRect(const NodeSpriteFrame &frame, int imageWidth, int imageHeight)
        {
            if (imageWidth <= 0 || imageHeight <= 0)
                return vec4(0.0f, 0.0f, 1.0f, 1.0f);

            const float invW = 1.0f / static_cast<float>(imageWidth);
            const float invH = 1.0f / static_cast<float>(imageHeight);
            return vec4(static_cast<float>(frame.x) * invW,
                        static_cast<float>(frame.y) * invH,
                        static_cast<float>(frame.x + frame.w) * invW,
                        static_cast<float>(frame.y + frame.h) * invH);
        }

        void SetError(std::string *outError, const std::string &error)
        {
            if (outError)
                *outError = error;
        }

        int FindClipIndex(const NodeSpriteComponent &sprite, const std::string &clipName)
        {
            if (!clipName.empty())
            {
                for (int i = 0; i < static_cast<int>(sprite.clips.size()); ++i)
                {
                    if (sprite.clips[i].name == clipName)
                        return i;
                }
                return -1;
            }

            if (sprite.activeClipIndex >= 0 && sprite.activeClipIndex < static_cast<int>(sprite.clips.size()))
                return sprite.activeClipIndex;
            return sprite.clips.empty() ? -1 : 0;
        }

        int ResolveMeshSlot(const NodeSpriteComponent &sprite, int requested)
        {
            return requested >= 0 ? requested : std::max(0, sprite.meshSlot);
        }

        int NextClipFrame(const NodeSpriteComponent &sprite, const NodeSpriteClip &clip)
        {
            int nextFrame = sprite.frameIndex < clip.start || sprite.frameIndex > clip.end
                                ? clip.start
                                : sprite.frameIndex + 1;
            if (nextFrame > clip.end)
                nextFrame = clip.loop && sprite.loop ? clip.start : clip.end;
            return nextFrame;
        }

        vec3 SpriteOutlinePosition(const NodeSpriteOutlineVertex &vertex, const AABB &quadBounds)
        {
            const vec3 center = quadBounds.GetCenter();
            const vec3 size = quadBounds.GetSize();
            return vec3(center.x + vertex.position.x * size.x,
                        center.y + vertex.position.y * size.y,
                        center.z);
        }

        void FillOutlineAabbVertices(std::vector<AabbVertex> &vertices, size_t offset, const AABB &aabb)
        {
            const vec3 corners[8] = {
                {aabb.min.x, aabb.min.y, aabb.min.z},
                {aabb.max.x, aabb.min.y, aabb.min.z},
                {aabb.max.x, aabb.max.y, aabb.min.z},
                {aabb.min.x, aabb.max.y, aabb.min.z},
                {aabb.min.x, aabb.min.y, aabb.max.z},
                {aabb.max.x, aabb.min.y, aabb.max.z},
                {aabb.max.x, aabb.max.y, aabb.max.z},
                {aabb.min.x, aabb.max.y, aabb.max.z},
            };
            for (size_t i = 0; i < 8; ++i)
            {
                vertices[offset + i].position[0] = corners[i].x;
                vertices[offset + i].position[1] = corners[i].y;
                vertices[offset + i].position[2] = corners[i].z;
            }
        }

        bool ApplySpriteFrame(Scene &scene,
                              NodeId *node,
                              NodeSpriteComponent &sprite,
                              int frameIndex,
                              int meshSlot,
                              bool transientGpuUpdate,
                              bool markDocumentDirty,
                              std::string *outError)
        {
            if (frameIndex < 0 || frameIndex >= static_cast<int>(sprite.frames.size()))
            {
                SetError(outError, "sprite frame index out of range");
                return false;
            }

            const NodeSpriteFrame &frame = sprite.frames[frameIndex];
            // Same frame re-apply (e.g. oneshot hold): skip UV + dirty work.
            if (sprite.frameIndex == frameIndex && sprite.uvRect == frame.uvRect && sprite.meshSlot == ResolveMeshSlot(sprite, meshSlot))
            {
                const auto &refs = scene.GetMeshRefs(node);
                if (sprite.meshSlot >= 0 && sprite.meshSlot < static_cast<int>(refs.size()))
                    scene.SetMeshSpriteFrameBlendTransient(refs[sprite.meshSlot], frame.uvRect, 0.0f);
                if (markDocumentDirty)
                    scene.MarkDirty();
                return true;
            }

            const int slot = ResolveMeshSlot(sprite, meshSlot);
            const auto &refs = scene.GetMeshRefs(node);
            if (slot < 0 || slot >= static_cast<int>(refs.size()))
            {
                SetError(outError, "sprite mesh slot out of range");
                return false;
            }

            const bool uvApplied = transientGpuUpdate ? scene.SetMeshUvRectTransient(refs[slot], frame.uvRect)
                                                      : scene.SetMeshUvRect(refs[slot], frame.uvRect);
            if (!uvApplied)
            {
                SetError(outError, "sprite mesh slot is not a four-vertex quad");
                return false;
            }
            scene.SetMeshSpriteFrameBlendTransient(refs[slot], frame.uvRect, 0.0f);
            if (!scene.SetSpriteOutlineFrame(node, frameIndex, transientGpuUpdate, outError))
                return false;

            sprite.frameIndex = frameIndex;
            sprite.frameName = frame.name;
            sprite.frameWidth = frame.w;
            sprite.frameHeight = frame.h;
            sprite.uvRect = frame.uvRect;
            sprite.meshSlot = slot;
            // Transient animation UV uploads stage their own GPU copies — do not
            // MarkNodeDirty (would force UpdateNodeMatrices for every walk frame).
            if (!transientGpuUpdate)
                scene.MarkNodeDirty(node);
            if (markDocumentDirty)
                scene.MarkDirty();
            return true;
        }

        ResourceHandle<Image> LoadSpriteImage(const std::string &path, float whitenAmount)
        {
            if (path.empty())
                return ResourceHandle<Image>();

            const std::filesystem::path resolved = ResolveSpritePath(path);
            std::error_code ec;
            if (resolved.empty() || !AssetFileExists(resolved))
                return ResourceHandle<Image>();

            std::filesystem::path normalized = std::filesystem::weakly_canonical(resolved, ec);
            if (ec)
                normalized = resolved;
            const std::string normalizedStr = PathUtf8(normalized);
            whitenAmount = std::isfinite(whitenAmount) ? std::clamp(whitenAmount, 0.0f, 1.0f) : 0.0f;
            const int whitenKey = static_cast<int>(std::lround(whitenAmount * 1000.0f));
            const std::string cacheKey =
                whitenKey > 0 ? "whiten:" + std::to_string(whitenKey) + ":" + normalizedStr : normalizedStr;

            // Cached atlases must not re-Submit+Wait a no-op command buffer —
            // pools that call sprite.setup on hundreds of bodies and each Wait
            // was a multi-ms hitch on spawn / Arena prewarm.
            if (ResourceHandle<Image> cached = ResourceManager::Get().Find<Image>(cacheKey))
                return cached;

            Queue *queue = RHII.GetMainQueue();
            if (!queue)
                return ResourceHandle<Image>();

            CommandBuffer *cmd = queue->AcquireCommandBuffer();
            cmd->Begin();
            ResourceHandle<Image> image;
            if (whitenKey > 0)
            {
                Image *rawImage = Image::LoadWhitenedRGBA8(cmd, normalizedStr, whitenAmount);
                if (rawImage)
                {
                    std::shared_ptr<Image> shared(rawImage, [](Image *p)
                                                  { Image::Destroy(p); });
                    ResourceManager::Get().Register<Image>(cacheKey, shared);
                    image = ResourceHandle<Image>(shared);
                }
            }
            else
            {
                ModelAsset loader;
                image = loader.LoadTexture(cmd, normalized);
            }
            cmd->End();
            if (!image)
            {
                queue->ReturnCommandBuffer(cmd);
                return image;
            }
            queue->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
            queue->ReturnCommandBuffer(cmd);
            return image;
        }

        struct CachedSpriteMetadata
        {
            std::string imagePath;
            int imageWidth = 0;
            int imageHeight = 0;
            bool interpolate = false;
            vec4 outlineColor = vec4(0.03f, 0.03f, 0.03f, 1.0f);
            std::vector<NodeSpriteFrame> frames;
            std::vector<std::vector<NodeSpriteOutlineVertex>> outlineFrames;
            std::vector<NodeSpriteClip> clips;
        };

        std::mutex &SpriteMetadataCacheMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        std::unordered_map<std::string, CachedSpriteMetadata> &SpriteMetadataCache()
        {
            static std::unordered_map<std::string, CachedSpriteMetadata> cache;
            return cache;
        }

        bool ParseSpriteOutlineFile(const std::filesystem::path &outlinePath,
                                    const std::vector<NodeSpriteFrame> &spriteFrames,
                                    std::vector<std::vector<NodeSpriteOutlineVertex>> &outFrames,
                                    vec4 &outColor,
                                    std::string *outError)
        {
            FileSystem in(PathUtf8(outlinePath), std::ios::in | std::ios::binary);
            if (!in.IsOpen())
            {
                SetError(outError, "sprite outline metadata not found: " + PathUtf8(outlinePath));
                return false;
            }

            const std::string text = in.ReadAll();
            rapidjson::Document root;
            root.Parse(text.c_str(), text.size());
            if (root.HasParseError() || !root.IsObject() ||
                !root.HasMember("frames") || !root["frames"].IsArray())
            {
                SetError(outError, "invalid sprite outline metadata: " + PathUtf8(outlinePath));
                return false;
            }

            if (root.HasMember("color") && root["color"].IsArray() && root["color"].Size() >= 4)
            {
                const auto &color = root["color"];
                outColor = vec4(std::clamp(ReadFloat(color[0], outColor.r), 0.0f, 1.0f),
                                std::clamp(ReadFloat(color[1], outColor.g), 0.0f, 1.0f),
                                std::clamp(ReadFloat(color[2], outColor.b), 0.0f, 1.0f),
                                std::clamp(ReadFloat(color[3], outColor.a), 0.0f, 1.0f));
            }

            const auto &frames = root["frames"];
            if (frames.Size() != spriteFrames.size())
            {
                SetError(outError, "sprite outline frame count does not match sprite metadata");
                return false;
            }

            outFrames.clear();
            outFrames.resize(spriteFrames.size());
            size_t expectedVertexCount = 0;
            for (rapidjson::SizeType i = 0; i < frames.Size(); ++i)
            {
                const auto &item = frames[i];
                if (!item.IsObject() || !item.HasMember("vertices") || !item["vertices"].IsArray())
                {
                    SetError(outError, "sprite outline frame is missing vertices");
                    return false;
                }
                if (item.HasMember("name") && item["name"].IsString() &&
                    spriteFrames[i].name != item["name"].GetString())
                {
                    SetError(outError, "sprite outline frame order does not match sprite metadata");
                    return false;
                }

                const auto &vertices = item["vertices"];
                if (vertices.Size() < 6 || (vertices.Size() & 1u) != 0u)
                {
                    SetError(outError, "sprite outline frames require paired inner/outer vertices");
                    return false;
                }
                if (expectedVertexCount == 0)
                    expectedVertexCount = vertices.Size();
                else if (expectedVertexCount != vertices.Size())
                {
                    SetError(outError, "sprite outline frames require a fixed vertex count");
                    return false;
                }

                auto &dst = outFrames[i];
                dst.reserve(vertices.Size());
                for (const auto &vertex : vertices.GetArray())
                {
                    if (!vertex.IsArray() || vertex.Size() < 3 ||
                        !vertex[0].IsNumber() || !vertex[1].IsNumber() || !vertex[2].IsNumber())
                    {
                        SetError(outError, "invalid sprite outline vertex");
                        return false;
                    }
                    NodeSpriteOutlineVertex parsed;
                    parsed.position.x = std::clamp(ReadFloat(vertex[0], 0.0f), -1.0f, 1.0f);
                    parsed.position.y = std::clamp(ReadFloat(vertex[1], 0.0f), -1.0f, 1.0f);
                    parsed.alpha = std::clamp(ReadFloat(vertex[2], 0.0f), 0.0f, 1.0f);
                    dst.push_back(parsed);
                }
            }
            return true;
        }

        bool ParseSpriteMetadataFile(const std::filesystem::path &metadataPath, CachedSpriteMetadata &out, std::string *outError)
        {
            FileSystem in(PathUtf8(metadataPath), std::ios::in | std::ios::binary);
            if (!in.IsOpen())
            {
                SetError(outError, "sprite metadata not found: " + PathUtf8(metadataPath));
                return false;
            }

            const std::string text = in.ReadAll();
            rapidjson::Document root;
            root.Parse(text.c_str(), text.size());
            if (root.HasParseError() || !root.IsObject())
            {
                SetError(outError, "invalid sprite metadata JSON: " + PathUtf8(metadataPath));
                return false;
            }

            out = {};
            if (root.HasMember("interpolate") && root["interpolate"].IsBool())
                out.interpolate = root["interpolate"].GetBool();
            if (root.HasMember("image") && root["image"].IsString())
            {
                const std::filesystem::path imagePath = ResolveMetadataRelativePath(metadataPath, root["image"].GetString());
                if (!imagePath.empty())
                    out.imagePath = PathUtf8(imagePath);
            }

            if (root.HasMember("image_size") && root["image_size"].IsObject())
            {
                const auto &imageSize = root["image_size"];
                if (imageSize.HasMember("width"))
                    out.imageWidth = std::max(0, ReadInt(imageSize["width"], out.imageWidth));
                if (imageSize.HasMember("height"))
                    out.imageHeight = std::max(0, ReadInt(imageSize["height"], out.imageHeight));
            }

            if (root.HasMember("frames") && root["frames"].IsArray())
            {
                const auto &frames = root["frames"];
                out.frames.reserve(frames.Size());
                for (rapidjson::SizeType i = 0; i < frames.Size(); ++i)
                {
                    const auto &item = frames[i];
                    if (!item.IsObject() || !item.HasMember("rect"))
                        continue;

                    NodeSpriteFrame frame;
                    frame.name = item.HasMember("name") && item["name"].IsString() ? item["name"].GetString()
                                                                                   : "frame_" + std::to_string(out.frames.size());
                    if (!ReadRect(item["rect"], frame.x, frame.y, frame.w, frame.h))
                        continue;
                    if (item.HasMember("pivot") && item["pivot"].IsArray() && item["pivot"].Size() >= 2)
                    {
                        frame.pivotX = std::clamp(ReadFloat(item["pivot"][0], frame.pivotX), 0.0f, 1.0f);
                        frame.pivotY = std::clamp(ReadFloat(item["pivot"][1], frame.pivotY), 0.0f, 1.0f);
                    }
                    if (item.HasMember("duration"))
                        frame.duration = std::max(0.01f, ReadFloat(item["duration"], frame.duration));
                    frame.uvRect = FrameUvRect(frame, out.imageWidth, out.imageHeight);
                    out.frames.push_back(std::move(frame));
                }
            }

            if (root.HasMember("outline") && root["outline"].IsString())
            {
                const std::filesystem::path outlinePath =
                    ResolveMetadataRelativePath(metadataPath, root["outline"].GetString());
                if (outlinePath.empty() ||
                    !ParseSpriteOutlineFile(outlinePath, out.frames, out.outlineFrames, out.outlineColor, outError))
                    return false;
            }

            if (root.HasMember("clips") && root["clips"].IsArray())
            {
                const auto &clips = root["clips"];
                out.clips.reserve(clips.Size());
                for (rapidjson::SizeType i = 0; i < clips.Size(); ++i)
                {
                    const auto &item = clips[i];
                    if (!item.IsObject())
                        continue;

                    NodeSpriteClip clip;
                    clip.name = item.HasMember("name") && item["name"].IsString() ? item["name"].GetString()
                                                                                  : "clip_" + std::to_string(out.clips.size());
                    clip.start = item.HasMember("start") ? std::max(0, ReadInt(item["start"], clip.start)) : clip.start;
                    clip.end = item.HasMember("end") ? std::max(0, ReadInt(item["end"], clip.end)) : clip.end;
                    clip.fps = item.HasMember("fps") ? std::max(0.1f, ReadFloat(item["fps"], clip.fps)) : clip.fps;
                    clip.loop = item.HasMember("loop") && item["loop"].IsBool() ? item["loop"].GetBool() : clip.loop;
                    if (!out.frames.empty())
                    {
                        const int maxFrame = static_cast<int>(out.frames.size()) - 1;
                        clip.start = std::clamp(clip.start, 0, maxFrame);
                        clip.end = std::clamp(clip.end, 0, maxFrame);
                        if (clip.start > clip.end)
                            std::swap(clip.start, clip.end);
                    }
                    out.clips.push_back(std::move(clip));
                }
            }

            if (out.clips.empty() && !out.frames.empty())
                out.clips.push_back({"default", 0, static_cast<int>(out.frames.size()) - 1, 10.0f, true});
            return true;
        }

        bool GetOrParseSpriteMetadata(const std::filesystem::path &metadataPath, CachedSpriteMetadata &out, std::string *outError)
        {
            const std::string key = PathUtf8(metadataPath);
            {
                std::lock_guard<std::mutex> lock(SpriteMetadataCacheMutex());
                auto it = SpriteMetadataCache().find(key);
                if (it != SpriteMetadataCache().end())
                {
                    out = it->second;
                    return true;
                }
            }

            CachedSpriteMetadata parsed;
            if (!ParseSpriteMetadataFile(metadataPath, parsed, outError))
                return false;

            std::lock_guard<std::mutex> lock(SpriteMetadataCacheMutex());
            auto [it, inserted] = SpriteMetadataCache().emplace(key, parsed);
            out = inserted ? parsed : it->second;
            return true;
        }

        bool ApplySpriteTextures(Scene &scene, NodeId *node, NodeSpriteComponent &sprite, int meshSlot, std::string *outError)
        {
            const auto &refs = scene.GetMeshRefs(node);
            if (meshSlot < 0 || meshSlot >= static_cast<int>(refs.size()))
            {
                SetError(outError, "sprite mesh slot out of range");
                return false;
            }

            const int meshIndex = refs[meshSlot];
            if (!scene.IsValidMeshIndex(meshIndex))
            {
                SetError(outError, "sprite mesh slot does not reference a valid mesh");
                return false;
            }

            Mesh &mesh = scene.GetMesh(meshIndex);
            if (!mesh.material)
            {
                SetError(outError, "sprite mesh has no material");
                return false;
            }

            ResourceHandle<Image> image = LoadSpriteImage(sprite.imagePath, sprite.whiten);
            if (!image)
            {
                SetError(outError, "sprite image not found: " + sprite.imagePath);
                return false;
            }

            if (sprite.imageWidth <= 0)
                sprite.imageWidth = static_cast<int>(image->GetWidth());
            if (sprite.imageHeight <= 0)
                sprite.imageHeight = static_cast<int>(image->GetHeight());

            const vec4 tint = sprite.tint;

            MaterialInstance *inst = mesh.materialInstance;
            if (inst)
            {
                // Pooled rigs re-run sprite.setup on every spawn: when the identical
                // sprite state is already bound, skip the dirty marks below — geometry
                // dirty alone costs a full geometry re-upload + BLAS/TLAS rebuild.
                const vec4 base = inst->GetBaseColorFactor();
                const vec3 emis = inst->GetEmissiveFactor();
                const uint32_t mask = (1u << static_cast<uint32_t>(TextureType::BaseColor)) |
                                      (1u << static_cast<uint32_t>(TextureType::Emissive));
                if (inst->GetTexture(static_cast<int>(TextureType::BaseColor)) == image.get() &&
                    inst->GetTexture(static_cast<int>(TextureType::Emissive)) == image.get() &&
                    (inst->GetTextureMask() & mask) == mask &&
                    inst->GetRenderType() == RenderType::AlphaCut &&
                    mesh.renderType == RenderType::AlphaCut &&
                    base.x == 0.0f && base.y == 0.0f && base.z == 0.0f && base.w == tint.a &&
                    emis.x == tint.x && emis.y == tint.y && emis.z == tint.z &&
                    inst->GetMetallic() == 1.0f && inst->GetRoughness() == 1.0f)
                {
                    return true;
                }
            }
            if (!inst)
                inst = scene.CreateMaterialInstance(mesh);
            if (!inst)
            {
                SetError(outError, "failed to create sprite material instance");
                return false;
            }
            inst->SetTexture(TextureType::BaseColor, image);
            inst->SetTexture(TextureType::Emissive, image);
            inst->SetTextureMask(inst->GetTextureMask() |
                                 (1u << static_cast<uint32_t>(TextureType::BaseColor)) |
                                 (1u << static_cast<uint32_t>(TextureType::Emissive)));
            inst->SetBaseColorFactor(vec4(0.0f, 0.0f, 0.0f, tint.a));
            inst->SetEmissiveFactor(vec3(tint));
            inst->SetMetallic(1.0f);
            inst->SetRoughness(1.0f);

            const bool renderTypeChanged = mesh.renderType != RenderType::AlphaCut;
            inst->SetRenderType(RenderType::AlphaCut);
            mesh.renderType = RenderType::AlphaCut;

            const bool baseColorCached =
                scene.TryBindCachedTexture(meshIndex, static_cast<int>(TextureType::BaseColor), image);
            const bool emissiveCached =
                scene.TryBindCachedTexture(meshIndex, static_cast<int>(TextureType::Emissive), image);
            if (!baseColorCached || !emissiveCached)
                scene.SetTexturesDirty();
            scene.SetMaterialDirty();
            if (renderTypeChanged)
                scene.SetInstancesDirty();
            scene.MarkNodeDirty(node);
            scene.MarkDirty();
            return true;
        }
    } // namespace

    bool Scene::LoadSpriteMetadata(NodeId *node, std::string *outError)
    {
        if (!node || !IsNodeAlive(node))
        {
            SetError(outError, "node not found");
            return false;
        }

        NodeSpriteComponent *sprite = GetSpriteComponent(node);
        if (!sprite)
        {
            SetError(outError, "node has no sprite component");
            return false;
        }

        const std::filesystem::path metadataPath = ResolveSpritePath(sprite->metadataPath);
        if (metadataPath.empty())
        {
            SetError(outError, "sprite metadata path is empty");
            return false;
        }

        CachedSpriteMetadata cached;
        if (!GetOrParseSpriteMetadata(metadataPath, cached, outError))
            return false;

        sprite->metadataPath = PathUtf8(metadataPath);
        sprite->imagePath = cached.imagePath;
        sprite->imageWidth = cached.imageWidth;
        sprite->imageHeight = cached.imageHeight;
        sprite->interpolate = cached.interpolate;
        sprite->outlineColor = cached.outlineColor;
        sprite->frames = cached.frames;
        sprite->outlineFrames = cached.outlineFrames;
        sprite->clips = cached.clips;
        // Recompute UVs in case image size was filled later from the texture.
        for (NodeSpriteFrame &frame : sprite->frames)
            frame.uvRect = FrameUvRect(frame, sprite->imageWidth, sprite->imageHeight);

        sprite->metadataLoaded = true;
        if (sprite->frameIndex >= static_cast<int>(sprite->frames.size()))
            sprite->frameIndex = sprite->frames.empty() ? -1 : 0;
        if (sprite->activeClipIndex >= static_cast<int>(sprite->clips.size()))
            sprite->activeClipIndex = -1;
        return true;
    }

    bool Scene::SetupSpriteFromMetadata(NodeId *node, const std::string &metadataPath, int meshSlot, std::string *outError)
    {
        if (!node || !IsNodeAlive(node))
        {
            SetError(outError, "node not found");
            return false;
        }
        if (metadataPath.empty())
        {
            SetError(outError, "sprite metadata path is empty");
            return false;
        }

        const auto &refs = GetMeshRefs(node);
        if (meshSlot < 0 || meshSlot >= static_cast<int>(refs.size()))
        {
            SetError(outError, "sprite mesh slot out of range");
            return false;
        }

        const int meshIndex = refs[meshSlot];
        if (!IsValidMeshIndex(meshIndex) || m_meshes[meshIndex].vertexCount != 4 ||
            static_cast<size_t>(m_meshes[meshIndex].vertexOffset) + 4 > m_vertexStore.size() ||
            static_cast<size_t>(m_meshes[meshIndex].positionsOffset) + 4 > m_positionUvStore.size())
        {
            SetError(outError, "sprite setup requires a four-vertex quad mesh");
            return false;
        }

        Mesh &mesh = m_meshes[meshIndex];
        for (int i = 0; i < static_cast<int>(m_meshes.size()); ++i)
        {
            const Mesh &other = m_meshes[i];
            if (i == meshIndex || !other.live || other.vertexOffset != mesh.vertexOffset ||
                other.positionsOffset != mesh.positionsOffset)
                continue;

            std::array<Vertex, 4> vertices;
            std::array<PositionUvVertex, 4> positions;
            std::copy_n(m_vertexStore.begin() + mesh.vertexOffset, 4, vertices.begin());
            std::copy_n(m_positionUvStore.begin() + mesh.positionsOffset, 4, positions.begin());
            mesh.vertexOffset = static_cast<uint32_t>(m_vertexStore.size());
            mesh.positionsOffset = static_cast<uint32_t>(m_positionUvStore.size());
            m_vertexStore.insert(m_vertexStore.end(), vertices.begin(), vertices.end());
            m_positionUvStore.insert(m_positionUvStore.end(), positions.begin(), positions.end());
            m_geometryDirty = true;
            break;
        }

        NodeSpriteComponent &sprite = GetOrCreateSpriteComponent(node);
        sprite.metadataPath = metadataPath;
        sprite.imagePath.clear();
        sprite.frames.clear();
        sprite.outlineFrames.clear();
        sprite.clips.clear();
        sprite.frameIndex = -1;
        sprite.frameName.clear();
        sprite.activeClipName.clear();
        sprite.activeClipIndex = -1;
        sprite.playbackAccumulator = 0.0f;
        sprite.playing = false;
        sprite.metadataLoaded = false;
        sprite.meshSlot = meshSlot;

        if (!LoadSpriteMetadata(node, outError))
            return false;

        if (!ApplySpriteTextures(*this, node, sprite, meshSlot, outError))
            return false;

        if (!EnsureSpriteOutlineMesh(node, sprite, outError))
            return false;

        // Recompute UVs now that image size may have been filled from the loaded texture.
        for (NodeSpriteFrame &frame : sprite.frames)
            frame.uvRect = FrameUvRect(frame, sprite.imageWidth, sprite.imageHeight);

        if (sprite.frames.empty())
        {
            SetError(outError, "sprite metadata has no frames: " + metadataPath);
            return false;
        }

        if (sprite.quadWidth <= 0.0f || sprite.quadHeight <= 0.0f)
        {
            const NodeSpriteFrame &frame0 = sprite.frames[0];
            const float fw = static_cast<float>(std::max(1, frame0.w));
            const float fh = static_cast<float>(std::max(1, frame0.h));
            sprite.quadWidth = fw >= fh ? 1.0f : fw / fh;
            sprite.quadHeight = fh >= fw ? 1.0f : fh / fw;
        }

        // Transient UV apply: the non-transient path force-marks full geometry dirty,
        // which costs a whole-scene re-upload + BLAS/TLAS rebuild when setup runs on
        // an already-live pooled quad. New quads are covered by their creation dirty.
        if (!ApplySpriteFrame(*this, node, sprite, 0, meshSlot, true, true, outError))
            return false;

        // Prefer named idle clip when present; otherwise leave on frame 0 stopped.
        for (const NodeSpriteClip &clip : sprite.clips)
        {
            if (clip.name == "idle")
                return PlaySpriteClip(node, "idle", true, meshSlot, outError);
        }
        return true;
    }

    bool Scene::EnsureSpriteOutlineMesh(NodeId *node, NodeSpriteComponent &sprite, std::string *outError)
    {
        if (!node || !IsNodeAlive(node))
        {
            SetError(outError, "node not found");
            return false;
        }

        if (sprite.outlineFrames.empty())
        {
            if (IsValidMeshIndex(sprite.outlineMeshIndex))
                RemoveMeshRef(node, sprite.outlineMeshIndex);
            sprite.outlineVertexCount = 0;
            return true;
        }
        if (sprite.outlineFrames.size() != sprite.frames.size() || sprite.outlineFrames[0].empty())
        {
            SetError(outError, "sprite outline frames do not match sprite frames");
            return false;
        }

        const uint32_t vertexCount = static_cast<uint32_t>(sprite.outlineFrames[0].size());
        const uint32_t pairCount = vertexCount / 2;
        const auto &refs = GetMeshRefs(node);
        if (sprite.meshSlot < 0 || sprite.meshSlot >= static_cast<int>(refs.size()) ||
            !IsValidMeshIndex(refs[sprite.meshSlot]))
        {
            SetError(outError, "sprite outline requires a valid sprite quad");
            return false;
        }
        const Mesh &quad = m_meshes[refs[sprite.meshSlot]];

        if (IsValidMeshIndex(sprite.outlineMeshIndex))
        {
            Mesh &outline = m_meshes[sprite.outlineMeshIndex];
            if (outline.vertexCount != vertexCount)
            {
                SetError(outError, "sprite outline vertex count changed; recreate the sprite node");
                return false;
            }
            if (std::find(refs.begin(), refs.end(), sprite.outlineMeshIndex) == refs.end())
                AddMeshRef(node, sprite.outlineMeshIndex);
            sprite.outlineVertexCount = vertexCount;
            return SetSpriteOutlineFrame(node, 0, true, outError);
        }

        const uint32_t vertexOffset = static_cast<uint32_t>(m_vertexStore.size());
        const uint32_t positionOffset = static_cast<uint32_t>(m_positionUvStore.size());
        const uint32_t indexOffset = static_cast<uint32_t>(m_indexStore.size());
        const size_t aabbOffset = m_aabbVertexStore.size();
        m_vertexStore.resize(m_vertexStore.size() + vertexCount);
        m_positionUvStore.resize(m_positionUvStore.size() + vertexCount);
        m_aabbVertexStore.resize(m_aabbVertexStore.size() + 8);
        m_indexStore.reserve(m_indexStore.size() + pairCount * 6);

        for (uint32_t i = 0; i < vertexCount; ++i)
        {
            const NodeSpriteOutlineVertex &source = sprite.outlineFrames[0][i];
            const vec3 position = SpriteOutlinePosition(source, quad.boundingBox);
            Vertex &vertex = m_vertexStore[vertexOffset + i];
            PositionUvVertex &positionUv = m_positionUvStore[positionOffset + i];
            FillVertexPosition(vertex, position.x, position.y, position.z);
            FillVertexNormal(vertex, 0.0f, 0.0f, 1.0f);
            FillVertexTangent(vertex, 1.0f, 0.0f, 0.0f, 1.0f);
            FillVertexUV(vertex, 0.0f, 0.0f);
            FillVertexColor(vertex, 1.0f, 1.0f, 1.0f, source.alpha);
            FillVertexPosition(positionUv, position.x, position.y, position.z);
            FillVertexUV(positionUv, source.alpha, 0.0f);
        }

        for (uint32_t i = 0; i < pairCount; ++i)
        {
            const uint32_t j = (i + 1) % pairCount;
            const uint32_t inner0 = i * 2;
            const uint32_t outer0 = inner0 + 1;
            const uint32_t inner1 = j * 2;
            const uint32_t outer1 = inner1 + 1;
            const uint32_t triangles[6] = {
                inner0,
                outer0,
                outer1,
                inner0,
                outer1,
                inner1,
            };
            m_indexStore.insert(m_indexStore.end(), std::begin(triangles), std::end(triangles));
        }

        AABB outlineBounds;
        outlineBounds.min = vec3(std::numeric_limits<float>::max());
        outlineBounds.max = vec3(std::numeric_limits<float>::lowest());
        for (const auto &frame : sprite.outlineFrames)
        {
            for (const NodeSpriteOutlineVertex &vertex : frame)
            {
                const vec3 position = SpriteOutlinePosition(vertex, quad.boundingBox);
                outlineBounds.min = min(outlineBounds.min, position);
                outlineBounds.max = max(outlineBounds.max, position);
            }
        }
        FillOutlineAabbVertices(m_aabbVertexStore, aabbOffset, outlineBounds);

        auto material = std::make_unique<Material>();
        material->name = "Sprite Outline";
        material->baseColorFactor = vec4(0.0f, 0.0f, 0.0f, sprite.outlineColor.a);
        material->emissiveFactor = vec3(sprite.outlineColor);
        material->metallic = 1.0f;
        material->roughness = 1.0f;
        material->renderType = RenderType::SpriteOutline;
        material->doubleSided = true;
        material->passInfoAsset =
            ResourceManager::Get().Load<PassInfoAsset>(Path::RuntimeAssets + "PassInfo/standard_pbr.pass");
        material->SyncParamsFromLegacy();

        Mesh outline{};
        outline.vertexOffset = vertexOffset;
        outline.vertexCount = vertexCount;
        outline.positionsOffset = positionOffset;
        outline.indexOffset = indexOffset;
        outline.indexCount = pairCount * 6;
        outline.aabbVertexOffset = aabbOffset;
        outline.boundingBox = outlineBounds;
        outline.renderType = RenderType::SpriteOutline;
        outline.material = material.get();
        outline.lodEnabled = false;
        outline.lodIndexOffset[0] = outline.indexOffset;
        outline.lodIndexCount[0] = outline.indexCount;

        m_ownedMaterials.push_back(std::move(material));
        sprite.outlineMeshIndex = AddMesh(std::move(outline));
        sprite.outlineVertexCount = vertexCount;
        if (static_cast<int>(m_meshSourceInfos.size()) <= sprite.outlineMeshIndex)
            m_meshSourceInfos.resize(sprite.outlineMeshIndex + 1);
        m_meshSourceInfos[sprite.outlineMeshIndex] = {};
        AddMeshRef(node, sprite.outlineMeshIndex);
        m_hasLinesMeshes = true;
        m_geometryDirty = true;
        m_materialDirty = true;
        return true;
    }

    bool Scene::SetSpriteOutlineFrame(NodeId *node, int frameIndex, bool transientGpuUpdate, std::string *outError)
    {
        NodeSpriteComponent *sprite = GetSpriteComponent(node);
        if (!sprite || sprite->outlineFrames.empty())
            return true;
        if (frameIndex < 0 || frameIndex >= static_cast<int>(sprite->outlineFrames.size()) ||
            !IsValidMeshIndex(sprite->outlineMeshIndex))
        {
            SetError(outError, "sprite outline frame is unavailable");
            return false;
        }

        Mesh &outline = m_meshes[sprite->outlineMeshIndex];
        const auto &vertices = sprite->outlineFrames[frameIndex];
        const auto &refs = GetMeshRefs(node);
        if (vertices.size() != outline.vertexCount || sprite->meshSlot < 0 ||
            sprite->meshSlot >= static_cast<int>(refs.size()) || !IsValidMeshIndex(refs[sprite->meshSlot]))
        {
            SetError(outError, "sprite outline geometry does not match its sprite quad");
            return false;
        }

        const AABB &quadBounds = m_meshes[refs[sprite->meshSlot]].boundingBox;
        bool changed = false;
        for (uint32_t i = 0; i < outline.vertexCount; ++i)
        {
            const vec3 position = SpriteOutlinePosition(vertices[i], quadBounds);
            Vertex &vertex = m_vertexStore[outline.vertexOffset + i];
            PositionUvVertex &positionUv = m_positionUvStore[outline.positionsOffset + i];
            changed |= vertex.position[0] != position.x || vertex.position[1] != position.y ||
                       vertex.position[2] != position.z || vertex.color[3] != vertices[i].alpha;
            FillVertexPosition(vertex, position.x, position.y, position.z);
            FillVertexColor(vertex, 1.0f, 1.0f, 1.0f, vertices[i].alpha);
            FillVertexPosition(positionUv, position.x, position.y, position.z);
            FillVertexUV(positionUv, vertices[i].alpha, 0.0f);
        }
        if (!changed)
            return true;
        if (transientGpuUpdate && m_buffer && !m_geometryDirty)
            m_pendingUvUploads.push_back(sprite->outlineMeshIndex);
        else
            m_geometryDirty = true;
        return true;
    }

    bool Scene::SetSpriteOutlineColor(NodeId *node, const vec4 &color)
    {
        NodeSpriteComponent *sprite = GetSpriteComponent(node);
        if (!sprite || !IsValidMeshIndex(sprite->outlineMeshIndex))
            return false;

        const vec4 safeColor = clamp(color, vec4(0.0f), vec4(1.0f));
        Mesh &mesh = m_meshes[sprite->outlineMeshIndex];
        MaterialInstance *instance = mesh.materialInstance ? mesh.materialInstance : CreateMaterialInstance(mesh);
        if (!instance)
            return false;
        const bool changed = instance->SetBaseColorFactor(vec4(0.0f, 0.0f, 0.0f, safeColor.a)) |
                             instance->SetEmissiveFactor(vec3(safeColor));
        if (changed)
            SetMaterialDirty();
        sprite->outlineColor = safeColor;
        return true;
    }

    NodeId *Scene::CreateSpriteNode(const std::string &name, NodeId *parent, const std::string &metadataPath,
                                    float quadWidth, float quadHeight, std::string *outError)
    {
        const float qw = quadWidth > 0.0f ? quadWidth : 1.0f;
        const float qh = quadHeight > 0.0f ? quadHeight : 1.0f;
        NodeId *node = CreateNode(name.empty() ? "Sprite" : name, parent);
        if (!node)
        {
            SetError(outError, "failed to create sprite node");
            return nullptr;
        }

        AttachPrimitiveToNode(node, Primitives::CreateQuad(qw, qh));
        NodeSpriteComponent &sprite = GetOrCreateSpriteComponent(node);
        sprite.quadWidth = qw;
        sprite.quadHeight = qh;

        if (!SetupSpriteFromMetadata(node, metadataPath, 0, outError))
            return node; // node exists; caller may fall back to plain texture

        return node;
    }

    bool Scene::SetSpriteFrame(NodeId *node, int frameIndex, int meshSlot, std::string *outError)
    {
        if (!node || !IsNodeAlive(node))
        {
            SetError(outError, "node not found");
            return false;
        }

        NodeSpriteComponent *sprite = GetSpriteComponent(node);
        if (!sprite)
        {
            SetError(outError, "node has no sprite component");
            return false;
        }

        if ((sprite->frames.empty() || !sprite->metadataLoaded) && !sprite->metadataPath.empty())
        {
            if (!LoadSpriteMetadata(node, outError))
                return false;
        }

        return ApplySpriteFrame(*this, node, *sprite, frameIndex, meshSlot, false, true, outError);
    }

    bool Scene::SetSpriteFrame(NodeId *node, const std::string &frameName, int meshSlot, std::string *outError)
    {
        if (frameName.empty())
        {
            SetError(outError, "sprite frame name is empty");
            return false;
        }

        NodeSpriteComponent *sprite = GetSpriteComponent(node);
        if (!sprite)
        {
            SetError(outError, "node has no sprite component");
            return false;
        }

        if ((sprite->frames.empty() || !sprite->metadataLoaded) && !sprite->metadataPath.empty())
        {
            if (!LoadSpriteMetadata(node, outError))
                return false;
        }

        for (int i = 0; i < static_cast<int>(sprite->frames.size()); ++i)
        {
            if (sprite->frames[i].name == frameName)
                return SetSpriteFrame(node, i, meshSlot, outError);
        }

        SetError(outError, "sprite frame name not found: " + frameName);
        return false;
    }

    bool Scene::PlaySpriteClip(NodeId *node, const std::string &clipName, bool restart, int meshSlot, std::string *outError)
    {
        if (!node || !IsNodeAlive(node))
        {
            SetError(outError, "node not found");
            return false;
        }

        NodeSpriteComponent *sprite = GetSpriteComponent(node);
        if (!sprite)
        {
            SetError(outError, "node has no sprite component");
            return false;
        }

        if ((sprite->frames.empty() || sprite->clips.empty() || !sprite->metadataLoaded) && !sprite->metadataPath.empty())
        {
            if (!LoadSpriteMetadata(node, outError))
                return false;
        }

        if (sprite->frames.empty() || sprite->clips.empty())
        {
            SetError(outError, "sprite has no frames or clips");
            return false;
        }

        const int clipIndex = FindClipIndex(*sprite, clipName.empty() ? sprite->activeClipName : clipName);
        if (clipIndex < 0)
        {
            SetError(outError, "sprite clip not found: " + clipName);
            return false;
        }

        const NodeSpriteClip &clip = sprite->clips[clipIndex];
        sprite->activeClipIndex = clipIndex;
        sprite->activeClipName = clip.name;
        sprite->loop = clip.loop;
        sprite->meshSlot = ResolveMeshSlot(*sprite, meshSlot);
        sprite->playing = true;

        const bool outsideClip = sprite->frameIndex < clip.start || sprite->frameIndex > clip.end;
        if (restart || outsideClip)
        {
            sprite->playbackAccumulator = 0.0f;
            if (!ApplySpriteFrame(*this, node, *sprite, clip.start, sprite->meshSlot, true, false, outError))
            {
                sprite->playing = false;
                return false;
            }
        }
        return true;
    }

    void Scene::SetSpritePlaying(NodeId *node, bool playing)
    {
        NodeSpriteComponent *sprite = GetSpriteComponent(node);
        if (!sprite)
            return;
        sprite->playing = playing;
        if (playing && sprite->activeClipIndex < 0 && !sprite->clips.empty())
            sprite->activeClipIndex = 0;
    }

    void Scene::SetSpriteInterpolation(NodeId *node, bool interpolate)
    {
        NodeSpriteComponent *sprite = GetSpriteComponent(node);
        if (!sprite)
            return;

        sprite->interpolate = interpolate;
        if (!interpolate && sprite->frameIndex >= 0 &&
            sprite->frameIndex < static_cast<int>(sprite->frames.size()))
        {
            const auto &refs = GetMeshRefs(node);
            if (sprite->meshSlot >= 0 && sprite->meshSlot < static_cast<int>(refs.size()))
                SetMeshSpriteFrameBlendTransient(refs[sprite->meshSlot],
                                                 sprite->frames[sprite->frameIndex].uvRect,
                                                 0.0f);
        }
        MarkDirty();
    }

    void Scene::StopSprite(NodeId *node)
    {
        NodeSpriteComponent *sprite = GetSpriteComponent(node);
        if (!sprite)
            return;

        sprite->playing = false;
        sprite->playbackAccumulator = 0.0f;
        if (sprite->activeClipIndex >= 0 && sprite->activeClipIndex < static_cast<int>(sprite->clips.size()))
        {
            std::string ignored;
            ApplySpriteFrame(*this, node, *sprite, sprite->clips[sprite->activeClipIndex].start, sprite->meshSlot, true, false, &ignored);
        }
    }

    void Scene::UpdateSpriteAnimations(float dt)
    {
        if (dt <= 0.0f)
            return;

        for (uint32_t i = 0; i < GetNodeCount(); ++i)
        {
            NodeId *node = GetNodeId(i);
            NodeSpriteComponent *sprite = GetSpriteComponent(node);
            if (!sprite || !sprite->playing)
                continue;

            if ((sprite->frames.empty() || sprite->clips.empty() || !sprite->metadataLoaded) && !sprite->metadataPath.empty())
            {
                std::string ignored;
                if (!LoadSpriteMetadata(node, &ignored))
                {
                    sprite->playing = false;
                    continue;
                }
            }

            int clipIndex = sprite->activeClipIndex;
            if (clipIndex < 0 || clipIndex >= static_cast<int>(sprite->clips.size()))
                clipIndex = FindClipIndex(*sprite, sprite->activeClipName);
            if (clipIndex < 0 || sprite->frames.empty())
            {
                sprite->playing = false;
                continue;
            }

            sprite->activeClipIndex = clipIndex;
            const NodeSpriteClip &clip = sprite->clips[clipIndex];
            const float step = 1.0f / std::max(0.1f, clip.fps);
            sprite->playbackAccumulator += dt * std::max(0.0f, sprite->playbackSpeed);

            while (sprite->playing && sprite->playbackAccumulator >= step)
            {
                sprite->playbackAccumulator -= step;
                int nextFrame = sprite->frameIndex < clip.start || sprite->frameIndex > clip.end
                                    ? clip.start
                                    : sprite->frameIndex + 1;
                if (nextFrame > clip.end)
                {
                    if (clip.loop && sprite->loop)
                    {
                        nextFrame = clip.start;
                    }
                    else
                    {
                        nextFrame = clip.end;
                        sprite->playing = false;
                    }
                }

                std::string ignored;
                if (!ApplySpriteFrame(*this, node, *sprite, nextFrame, sprite->meshSlot, true, false, &ignored))
                {
                    sprite->playing = false;
                    break;
                }
            }

            if (sprite->interpolate && sprite->frameIndex >= 0 &&
                sprite->frameIndex < static_cast<int>(sprite->frames.size()))
            {
                const auto &refs = GetMeshRefs(node);
                if (sprite->meshSlot >= 0 && sprite->meshSlot < static_cast<int>(refs.size()))
                {
                    const int nextFrame = NextClipFrame(*sprite, clip);
                    const float blend = sprite->playing ? sprite->playbackAccumulator / step : 0.0f;
                    SetMeshSpriteFrameBlendTransient(refs[sprite->meshSlot],
                                                     sprite->frames[nextFrame].uvRect,
                                                     blend);
                }
            }
        }
    }
} // namespace pe
