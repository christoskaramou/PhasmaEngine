#include "GUI/SpriteAuthoring.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Scene/Material.h"
#include "Scene/ModelAsset.h"
#include "Scene/Primitives.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include <nlohmann/json.hpp>

namespace pe
{
    namespace SpriteAuthoring
    {
        bool IsSpriteMetadataFile(const std::filesystem::path &path);
        bool IsImageFile(const std::filesystem::path &path);

        namespace
        {
            struct Frame
            {
                std::string name;
                int x = 0;
                int y = 0;
                int w = 0;
                int h = 0;
            };

            struct SpriteAsset
            {
                std::filesystem::path metadataPath;
                std::filesystem::path imagePath;
                int imageWidth = 0;
                int imageHeight = 0;
                std::vector<Frame> frames;
                std::string error;
            };

            std::string PathUtf8(const std::filesystem::path &path)
            {
                const auto u8 = path.u8string();
                return std::string(reinterpret_cast<const char *>(u8.c_str()), u8.size());
            }

            std::filesystem::path U8Path(const std::string &utf8)
            {
                return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
            }

            std::filesystem::path ExistingCandidate(std::initializer_list<std::filesystem::path> candidates)
            {
                for (const auto &candidate : candidates)
                {
                    std::error_code ec;
                    if (std::filesystem::exists(candidate, ec))
                        return candidate.lexically_normal();
                }
                return candidates.size() ? candidates.begin()->lexically_normal() : std::filesystem::path();
            }

            std::filesystem::path ResolveAssetPath(const std::filesystem::path &path)
            {
                if (path.empty())
                    return {};
                if (path.is_absolute())
                    return path.lexically_normal();

                const std::filesystem::path assets = U8Path(Path::Assets);
                return ExistingCandidate({path, assets / path, U8Path(Path::Root) / path});
            }

            std::filesystem::path ResolveTexturePath(const std::filesystem::path &path)
            {
                if (path.empty())
                    return {};
                if (path.is_absolute())
                    return path.lexically_normal();

                const std::filesystem::path assets = U8Path(Path::Assets);
                return ExistingCandidate({path, assets / path, assets / "Textures" / path, assets / "Objects" / path});
            }

            std::filesystem::path ResolvePathFromMetadata(const std::filesystem::path &metadataPath, const std::string &value)
            {
                if (value.empty())
                    return {};

                std::filesystem::path p = U8Path(value);
                if (p.is_absolute())
                    return p.lexically_normal();

                const std::filesystem::path assets = U8Path(Path::Assets);
                return ExistingCandidate({assets / p, metadataPath.parent_path() / p, p});
            }

            int SafeInt(const nlohmann::json &value, int fallback = 0)
            {
                return value.is_number_integer() ? value.get<int>() : fallback;
            }

            SpriteAsset LoadSpriteAsset(const std::filesystem::path &assetPath)
            {
                SpriteAsset asset;
                const std::filesystem::path resolved = ResolveAssetPath(assetPath);
                if (resolved.empty())
                    return asset;

                if (!IsSpriteAssetPath(resolved))
                {
                    asset.error = "not a sprite metadata or image asset: " + PathUtf8(assetPath);
                    return asset;
                }

                if (!IsSpriteMetadataFile(resolved))
                {
                    asset.imagePath = ResolveTexturePath(resolved);
                    return asset;
                }

                asset.metadataPath = resolved;
                std::ifstream in(resolved, std::ios::binary);
                if (!in)
                {
                    asset.error = "failed to open sprite metadata: " + PathUtf8(assetPath);
                    return asset;
                }

                nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
                if (root.is_discarded() || !root.is_object())
                {
                    asset.error = "invalid sprite metadata JSON: " + PathUtf8(assetPath);
                    return asset;
                }

                asset.imagePath = ResolvePathFromMetadata(resolved, root.value("image", ""));
                if (root.contains("image_size") && root["image_size"].is_object())
                {
                    asset.imageWidth = SafeInt(root["image_size"].value("width", nlohmann::json()), 0);
                    asset.imageHeight = SafeInt(root["image_size"].value("height", nlohmann::json()), 0);
                }

                if (root.contains("frames") && root["frames"].is_array())
                {
                    for (const auto &item : root["frames"])
                    {
                        if (!item.is_object() || !item.contains("rect") || !item["rect"].is_array() || item["rect"].size() < 4)
                            continue;

                        Frame frame;
                        frame.name = item.value("name", "");
                        const auto &rect = item["rect"];
                        frame.x = SafeInt(rect[0]);
                        frame.y = SafeInt(rect[1]);
                        frame.w = SafeInt(rect[2]);
                        frame.h = SafeInt(rect[3]);
                        if (frame.w > 0 && frame.h > 0)
                            asset.frames.push_back(std::move(frame));
                    }
                }

                return asset;
            }

            ResourceHandle<Image> LoadTexture(const std::filesystem::path &path, std::string &outError)
            {
                if (path.empty())
                    return {};

                Queue *queue = RHII.GetMainQueue();
                if (!queue)
                {
                    outError = "main queue not available";
                    return {};
                }

                std::error_code ec;
                if (!std::filesystem::exists(path, ec))
                {
                    outError = "texture file not found: " + PathUtf8(path);
                    return {};
                }

                CommandBuffer *cmd = queue->AcquireCommandBuffer();
                cmd->Begin();
                ModelAsset loader;
                ResourceHandle<Image> image = loader.LoadTexture(cmd, path);
                cmd->End();
                queue->Submit(1, &cmd, nullptr, nullptr);
                cmd->Wait();
                queue->ReturnCommandBuffer(cmd);

                if (!image)
                    outError = "failed to load texture: " + PathUtf8(path);
                return image;
            }

            const Frame *SelectFrame(const SpriteAsset &asset, const Options &options, int &outFrameIndex)
            {
                outFrameIndex = -1;
                if (asset.frames.empty())
                    return nullptr;

                if (!options.frameName.empty())
                {
                    for (size_t i = 0; i < asset.frames.size(); ++i)
                    {
                        if (asset.frames[i].name == options.frameName)
                        {
                            outFrameIndex = static_cast<int>(i);
                            return &asset.frames[i];
                        }
                    }
                }

                int index = options.frameIndex >= 0 ? options.frameIndex : 0;
                if (index < 0 || index >= static_cast<int>(asset.frames.size()))
                    index = 0;
                outFrameIndex = index;
                return &asset.frames[index];
            }

            vec4 FrameUvRect(const Frame &frame, int imageWidth, int imageHeight)
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

            std::string DefaultNodeName(const SpriteAsset &asset, const Options &options)
            {
                if (!options.name.empty())
                    return options.name;

                const std::filesystem::path source = !asset.metadataPath.empty() ? asset.metadataPath : asset.imagePath;
                if (source.empty())
                    return "Sprite";

                std::string stem = source.stem().string();
                if (stem.ends_with(".sprite"))
                    stem.resize(stem.size() - 7);
                return stem.empty() ? "Sprite" : "Sprite " + stem;
            }

            void ApplyTransformDirty(Scene &scene, NodeId *node)
            {
                scene.SetTexturesDirty();
                scene.SetMaterialDirty();
                scene.SetGeometryDirty();
                scene.MarkNodeDirty(node);
                scene.MarkDirty();
            }

            bool ApplyTextureAndMaterial(Scene &scene, NodeId *node, const ResourceHandle<Image> &image, const Options &options, int meshSlot, std::string &outError)
            {
                const auto &refs = scene.GetMeshRefs(node);
                if (meshSlot < 0 || meshSlot >= static_cast<int>(refs.size()))
                {
                    outError = "mesh slot out of range";
                    return false;
                }

                const int meshIndex = refs[meshSlot];
                if (!scene.IsValidMeshIndex(meshIndex))
                {
                    outError = "mesh slot does not reference a valid mesh";
                    return false;
                }

                Mesh &mesh = scene.GetMesh(meshIndex);
                if (!mesh.material)
                {
                    outError = "mesh has no material";
                    return false;
                }

                MaterialInstance *inst = mesh.materialInstance;
                if (!inst)
                    inst = scene.CreateMaterialInstance(mesh);
                if (!inst)
                {
                    outError = "failed to create material instance";
                    return false;
                }

                if (image)
                {
                    inst->SetTexture(TextureType::BaseColor, image);
                    inst->SetTexture(TextureType::Emissive, image);
                    inst->SetTextureMask(inst->GetTextureMask() |
                                         (1u << static_cast<uint32_t>(TextureType::BaseColor)) |
                                         (1u << static_cast<uint32_t>(TextureType::Emissive)));
                }
                const vec4 tint = options.hasTint ? options.tint : vec4(1.0f);
                inst->SetBaseColorFactor(vec4(0.0f, 0.0f, 0.0f, tint.a));
                inst->SetEmissiveFactor(vec3(tint));

                inst->SetMetallic(1.0f);
                inst->SetRoughness(1.0f);
                inst->SetRenderType(RenderType::AlphaBlend);
                mesh.renderType = RenderType::AlphaBlend;
                ApplyTransformDirty(scene, node);
                return true;
            }

            void PopulateFrameResult(Result &result, const SpriteAsset &asset, const Frame *frame, int frameIndex)
            {
                result.imagePath = asset.imagePath;
                result.metadataPath = asset.metadataPath;
                result.imageWidth = asset.imageWidth;
                result.imageHeight = asset.imageHeight;
                result.frameIndex = frame ? frameIndex : -1;
                if (frame)
                {
                    result.frameName = frame->name;
                    result.frameWidth = frame->w;
                    result.frameHeight = frame->h;
                }
                else
                {
                    result.frameWidth = asset.imageWidth;
                    result.frameHeight = asset.imageHeight;
                }
            }

            void StoreSpriteComponent(Scene &scene, NodeId *node, const Result &result, const Options &options, bool replaceAsset, int meshSlot)
            {
                if (!node || !scene.IsNodeAlive(node))
                    return;

                NodeSpriteComponent &sprite = scene.GetOrCreateSpriteComponent(node);
                const bool metadataChanged = replaceAsset && sprite.metadataPath != PathUtf8(result.metadataPath);
                if (replaceAsset || !result.imagePath.empty())
                    sprite.imagePath = PathUtf8(result.imagePath);
                if (replaceAsset || !result.metadataPath.empty())
                    sprite.metadataPath = PathUtf8(result.metadataPath);

                if (replaceAsset || result.frameIndex >= 0)
                {
                    sprite.frameIndex = result.frameIndex;
                    sprite.frameName = result.frameName;
                    sprite.imageWidth = result.imageWidth;
                    sprite.imageHeight = result.imageHeight;
                    sprite.frameWidth = result.frameWidth;
                    sprite.frameHeight = result.frameHeight;
                }

                if (options.hasUvRect || result.frameIndex >= 0 || replaceAsset)
                    sprite.uvRect = result.uvRect;
                if (options.hasTint)
                    sprite.tint = options.tint;
                if (result.quadWidth > 0.0f && result.quadHeight > 0.0f)
                {
                    sprite.quadWidth = result.quadWidth;
                    sprite.quadHeight = result.quadHeight;
                }
                sprite.meshSlot = std::max(0, meshSlot);
                if (metadataChanged)
                {
                    sprite.frames.clear();
                    sprite.clips.clear();
                    sprite.activeClipName.clear();
                    sprite.activeClipIndex = -1;
                    sprite.playing = false;
                    sprite.playbackAccumulator = 0.0f;
                    sprite.metadataLoaded = false;
                }
            }

            float NormalizedDimension(int value, int other)
            {
                const float maxDim = static_cast<float>(std::max(value, other));
                return maxDim > 0.0f ? static_cast<float>(value) / maxDim : 1.0f;
            }
        } // namespace

        bool IsSpriteAssetPath(const std::filesystem::path &path)
        {
            return IsSpriteMetadataFile(path) || IsImageFile(path);
        }

        bool IsSpriteMetadataFile(const std::filesystem::path &path)
        {
            return ToLower(path.filename().string()).ends_with(".sprite.json");
        }

        bool IsImageFile(const std::filesystem::path &path)
        {
            static const std::unordered_set<std::string> exts = {
                ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr", ".dds", ".ktx", ".ktx2"};
            return exts.find(ToLower(path.extension().string())) != exts.end();
        }

        Result CreateNode(Scene &scene, const Options &options, NodeId *parent)
        {
            Result result;
            SpriteAsset asset = LoadSpriteAsset(options.assetPath);
            if (!asset.error.empty())
            {
                result.error = asset.error;
                return result;
            }

            std::string loadError;
            ResourceHandle<Image> image = LoadTexture(asset.imagePath, loadError);
            if (!asset.imagePath.empty() && !image)
            {
                result.error = loadError;
                return result;
            }
            if (image)
            {
                asset.imageWidth = image->GetWidth();
                asset.imageHeight = image->GetHeight();
            }

            int frameIndex = -1;
            const Frame *frame = SelectFrame(asset, options, frameIndex);
            PopulateFrameResult(result, asset, frame, frameIndex);

            const int frameWidth = result.frameWidth > 0 ? result.frameWidth : 1;
            const int frameHeight = result.frameHeight > 0 ? result.frameHeight : 1;
            result.quadWidth = NormalizedDimension(frameWidth, frameHeight);
            result.quadHeight = NormalizedDimension(frameHeight, frameWidth);

            NodeId *node = scene.CreateNode(DefaultNodeName(asset, options), parent);
            scene.AttachPrimitiveToNode(node, Primitives::CreateQuad(result.quadWidth, result.quadHeight));
            result.node = node;

            if (frame)
                result.uvRect = FrameUvRect(*frame, asset.imageWidth, asset.imageHeight);
            else
                result.uvRect = options.hasUvRect ? options.uvRect : vec4(0.0f, 0.0f, 1.0f, 1.0f);

            if (!ApplyTextureAndMaterial(scene, node, image, options, 0, result.error))
                return result;
            if (frame || options.hasUvRect)
                ApplyUvRect(scene, node, result.uvRect, 0, result.error);

            StoreSpriteComponent(scene, node, result, options, true, 0);
            ApplyTransformDirty(scene, node);
            return result;
        }

        Result ApplyToNode(Scene &scene, NodeId *node, const Options &options, int meshSlot)
        {
            Result result;
            result.node = node;
            if (!node || !scene.IsNodeAlive(node))
            {
                result.error = "node not found";
                return result;
            }

            SpriteAsset asset;
            ResourceHandle<Image> image;
            if (!options.assetPath.empty())
            {
                asset = LoadSpriteAsset(options.assetPath);
                if (!asset.error.empty())
                {
                    result.error = asset.error;
                    return result;
                }

                std::string loadError;
                image = LoadTexture(asset.imagePath, loadError);
                if (!asset.imagePath.empty() && !image)
                {
                    result.error = loadError;
                    return result;
                }
                if (image)
                {
                    asset.imageWidth = image->GetWidth();
                    asset.imageHeight = image->GetHeight();
                }
            }

            int frameIndex = -1;
            const Frame *frame = SelectFrame(asset, options, frameIndex);
            PopulateFrameResult(result, asset, frame, frameIndex);
            const NodeSpriteComponent *existingSprite = scene.GetSpriteComponent(node);
            if (existingSprite && options.assetPath.empty())
            {
                result.imagePath = U8Path(existingSprite->imagePath);
                result.metadataPath = U8Path(existingSprite->metadataPath);
                result.imageWidth = existingSprite->imageWidth;
                result.imageHeight = existingSprite->imageHeight;
                result.frameIndex = existingSprite->frameIndex;
                result.frameName = existingSprite->frameName;
                result.frameWidth = existingSprite->frameWidth;
                result.frameHeight = existingSprite->frameHeight;
            }
            if (existingSprite)
            {
                result.quadWidth = existingSprite->quadWidth;
                result.quadHeight = existingSprite->quadHeight;
            }
            else
            {
                const int frameWidth = result.frameWidth > 0 ? result.frameWidth : result.imageWidth;
                const int frameHeight = result.frameHeight > 0 ? result.frameHeight : result.imageHeight;
                if (frameWidth > 0 && frameHeight > 0)
                {
                    result.quadWidth = NormalizedDimension(frameWidth, frameHeight);
                    result.quadHeight = NormalizedDimension(frameHeight, frameWidth);
                }
            }
            if (options.hasUvRect)
                result.uvRect = options.uvRect;
            else if (frame)
                result.uvRect = FrameUvRect(*frame, asset.imageWidth, asset.imageHeight);
            else if (existingSprite && options.assetPath.empty())
                result.uvRect = existingSprite->uvRect;
            else
                result.uvRect = vec4(0.0f, 0.0f, 1.0f, 1.0f);

            if ((image || options.hasTint) && !ApplyTextureAndMaterial(scene, node, image, options, meshSlot, result.error))
                return result;

            if (frame || options.hasUvRect)
                ApplyUvRect(scene, node, result.uvRect, meshSlot, result.error);

            if (result.error.empty())
            {
                StoreSpriteComponent(scene, node, result, options, !options.assetPath.empty(), meshSlot);
                ApplyTransformDirty(scene, node);
            }
            return result;
        }

        bool ApplyUvRect(Scene &scene, NodeId *node, const vec4 &uvRect, int meshSlot, std::string &outError)
        {
            if (!node || !scene.IsNodeAlive(node))
            {
                outError = "node not found";
                return false;
            }

            const auto &refs = scene.GetMeshRefs(node);
            if (meshSlot < 0 || meshSlot >= static_cast<int>(refs.size()))
            {
                outError = "mesh slot out of range";
                return false;
            }

            if (!scene.SetMeshUvRect(refs[meshSlot], uvRect))
            {
                outError = "mesh slot is not a four-vertex quad";
                return false;
            }

            scene.MarkNodeDirty(node);
            scene.SetGeometryDirty();
            scene.MarkDirty();
            return true;
        }
    } // namespace SpriteAuthoring
} // namespace pe
