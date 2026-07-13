#include "Scene/Scene.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Scene/Material.h"
#include "Scene/ModelAsset.h"
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
                std::error_code ec;
                if (!candidate.empty() && std::filesystem::exists(candidate, ec))
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

            const int slot = ResolveMeshSlot(sprite, meshSlot);
            const auto &refs = scene.GetMeshRefs(node);
            if (slot < 0 || slot >= static_cast<int>(refs.size()))
            {
                SetError(outError, "sprite mesh slot out of range");
                return false;
            }

            const NodeSpriteFrame &frame = sprite.frames[frameIndex];
            const bool uvApplied = transientGpuUpdate ? scene.SetMeshUvRectTransient(refs[slot], frame.uvRect)
                                                      : scene.SetMeshUvRect(refs[slot], frame.uvRect);
            if (!uvApplied)
            {
                SetError(outError, "sprite mesh slot is not a four-vertex quad");
                return false;
            }

            sprite.frameIndex = frameIndex;
            sprite.frameName = frame.name;
            sprite.frameWidth = frame.w;
            sprite.frameHeight = frame.h;
            sprite.uvRect = frame.uvRect;
            sprite.meshSlot = slot;
            scene.MarkNodeDirty(node);
            if (markDocumentDirty)
                scene.MarkDirty();
            return true;
        }

        ResourceHandle<Image> LoadSpriteImage(const std::string &path)
        {
            if (path.empty())
                return ResourceHandle<Image>();

            Queue *queue = RHII.GetMainQueue();
            if (!queue)
                return ResourceHandle<Image>();

            const std::filesystem::path resolved = ResolveSpritePath(path);
            std::error_code ec;
            if (resolved.empty() || !std::filesystem::exists(resolved, ec))
                return ResourceHandle<Image>();

            CommandBuffer *cmd = queue->AcquireCommandBuffer();
            cmd->Begin();
            ModelAsset loader;
            ResourceHandle<Image> image = loader.LoadTexture(cmd, resolved);
            cmd->End();
            queue->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
            queue->ReturnCommandBuffer(cmd);
            return image;
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

            ResourceHandle<Image> image = LoadSpriteImage(sprite.imagePath);
            if (!image)
            {
                SetError(outError, "sprite image not found: " + sprite.imagePath);
                return false;
            }

            if (sprite.imageWidth <= 0)
                sprite.imageWidth = static_cast<int>(image->GetWidth());
            if (sprite.imageHeight <= 0)
                sprite.imageHeight = static_cast<int>(image->GetHeight());

            MaterialInstance *inst = mesh.materialInstance;
            if (!inst)
                inst = scene.CreateMaterialInstance(mesh);
            if (!inst)
            {
                SetError(outError, "failed to create sprite material instance");
                return false;
            }

            const vec4 tint = sprite.tint;
            inst->SetTexture(TextureType::BaseColor, image);
            inst->SetTexture(TextureType::Emissive, image);
            inst->SetTextureMask(inst->GetTextureMask() |
                                 (1u << static_cast<uint32_t>(TextureType::BaseColor)) |
                                 (1u << static_cast<uint32_t>(TextureType::Emissive)));
            inst->SetBaseColorFactor(vec4(0.0f, 0.0f, 0.0f, tint.a));
            inst->SetEmissiveFactor(vec3(tint));
            inst->SetMetallic(1.0f);
            inst->SetRoughness(1.0f);
            // AlphaCut matches ATH top-down flat sprites; Lua can still override.
            inst->SetRenderType(RenderType::AlphaCut);
            mesh.renderType = RenderType::AlphaCut;

            scene.SetTexturesDirty();
            scene.SetMaterialDirty();
            scene.SetGeometryDirty();
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

        std::ifstream in(metadataPath, std::ios::binary);
        if (!in)
        {
            SetError(outError, "sprite metadata not found: " + sprite->metadataPath);
            return false;
        }

        rapidjson::IStreamWrapper stream(in);
        rapidjson::Document root;
        root.ParseStream(stream);
        if (root.HasParseError() || !root.IsObject())
        {
            SetError(outError, "invalid sprite metadata JSON: " + PathUtf8(metadataPath));
            return false;
        }

        sprite->metadataPath = PathUtf8(metadataPath);
        if (root.HasMember("image") && root["image"].IsString())
        {
            const std::filesystem::path imagePath = ResolveMetadataRelativePath(metadataPath, root["image"].GetString());
            if (!imagePath.empty())
                sprite->imagePath = PathUtf8(imagePath);
        }

        if (root.HasMember("image_size") && root["image_size"].IsObject())
        {
            const auto &imageSize = root["image_size"];
            if (imageSize.HasMember("width"))
                sprite->imageWidth = std::max(0, ReadInt(imageSize["width"], sprite->imageWidth));
            if (imageSize.HasMember("height"))
                sprite->imageHeight = std::max(0, ReadInt(imageSize["height"], sprite->imageHeight));
        }

        sprite->frames.clear();
        if (root.HasMember("frames") && root["frames"].IsArray())
        {
            const auto &frames = root["frames"];
            sprite->frames.reserve(frames.Size());
            for (rapidjson::SizeType i = 0; i < frames.Size(); ++i)
            {
                const auto &item = frames[i];
                if (!item.IsObject() || !item.HasMember("rect"))
                    continue;

                NodeSpriteFrame frame;
                frame.name = item.HasMember("name") && item["name"].IsString() ? item["name"].GetString() : "frame_" + std::to_string(sprite->frames.size());
                if (!ReadRect(item["rect"], frame.x, frame.y, frame.w, frame.h))
                    continue;
                if (item.HasMember("pivot") && item["pivot"].IsArray() && item["pivot"].Size() >= 2)
                {
                    frame.pivotX = std::clamp(ReadFloat(item["pivot"][0], frame.pivotX), 0.0f, 1.0f);
                    frame.pivotY = std::clamp(ReadFloat(item["pivot"][1], frame.pivotY), 0.0f, 1.0f);
                }
                if (item.HasMember("duration"))
                    frame.duration = std::max(0.01f, ReadFloat(item["duration"], frame.duration));
                frame.uvRect = FrameUvRect(frame, sprite->imageWidth, sprite->imageHeight);
                sprite->frames.push_back(std::move(frame));
            }
        }

        sprite->clips.clear();
        if (root.HasMember("clips") && root["clips"].IsArray())
        {
            const auto &clips = root["clips"];
            sprite->clips.reserve(clips.Size());
            for (rapidjson::SizeType i = 0; i < clips.Size(); ++i)
            {
                const auto &item = clips[i];
                if (!item.IsObject())
                    continue;

                NodeSpriteClip clip;
                clip.name = item.HasMember("name") && item["name"].IsString() ? item["name"].GetString() : "clip_" + std::to_string(sprite->clips.size());
                clip.start = item.HasMember("start") ? std::max(0, ReadInt(item["start"], clip.start)) : clip.start;
                clip.end = item.HasMember("end") ? std::max(0, ReadInt(item["end"], clip.end)) : clip.end;
                clip.fps = item.HasMember("fps") ? std::max(0.1f, ReadFloat(item["fps"], clip.fps)) : clip.fps;
                clip.loop = item.HasMember("loop") && item["loop"].IsBool() ? item["loop"].GetBool() : clip.loop;
                if (!sprite->frames.empty())
                {
                    const int maxFrame = static_cast<int>(sprite->frames.size()) - 1;
                    clip.start = std::clamp(clip.start, 0, maxFrame);
                    clip.end = std::clamp(clip.end, 0, maxFrame);
                    if (clip.start > clip.end)
                        std::swap(clip.start, clip.end);
                }
                sprite->clips.push_back(std::move(clip));
            }
        }

        if (sprite->clips.empty() && !sprite->frames.empty())
            sprite->clips.push_back({"default", 0, static_cast<int>(sprite->frames.size()) - 1, 10.0f, true});

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

        NodeSpriteComponent &sprite = GetOrCreateSpriteComponent(node);
        sprite.metadataPath = metadataPath;
        sprite.imagePath.clear();
        sprite.frames.clear();
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

        if (!SetSpriteFrame(node, 0, meshSlot, outError))
            return false;

        // Prefer named idle clip when present; otherwise leave on frame 0 stopped.
        for (const NodeSpriteClip &clip : sprite.clips)
        {
            if (clip.name == "idle")
                return PlaySpriteClip(node, "idle", true, meshSlot, outError);
        }
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
                int nextFrame = sprite->frameIndex < clip.start || sprite->frameIndex > clip.end ? clip.start : sprite->frameIndex + 1;
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
        }
    }
} // namespace pe
