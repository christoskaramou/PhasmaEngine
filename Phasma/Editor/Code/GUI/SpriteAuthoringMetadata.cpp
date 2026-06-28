#include "GUI/SpriteAuthoring.h"
#include "Phasma/MCP/Utils.h"
#include "Scene/NodeComponents.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "stb_image.h"
#include <nlohmann/json.hpp>

namespace pe
{
    namespace SpriteAuthoring
    {
        namespace
        {
            static std::string PathUtf8(const std::filesystem::path &path)
            {
                const auto u8 = path.u8string();
                return std::string(reinterpret_cast<const char *>(u8.c_str()), u8.size());
            }

            static std::filesystem::path U8Path(const std::string &utf8)
            {
                return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
            }

            // Simple resolver: relative paths become Assets/<path> (no existence probe).
            // Used by ResolveSpriteMetadataPath / ResolveSpriteSheetPath where we want a
            // deterministic destination regardless of whether the file exists yet.
            static std::filesystem::path ResolveAssetPath(const std::string &path)
            {
                std::filesystem::path p(path);
                if (p.is_absolute())
                    return p;
                return U8Path(Path::Assets) / U8Path(path);
            }

            // Texture resolver: probes several well-known subdirectories.
            static std::filesystem::path ResolveTexturePath(const std::string &path)
            {
                if (path.empty())
                    return {};
                std::filesystem::path p = U8Path(path);
                if (p.is_absolute())
                    return p;

                const std::filesystem::path assets = U8Path(Path::Assets);
                const std::filesystem::path candidates[4] = {
                    assets / p, assets / "Textures" / p, assets / "Objects" / p, p};

                for (const auto &candidate : candidates)
                {
                    std::error_code ec;
                    if (std::filesystem::exists(candidate, ec))
                        return candidate;
                }
                return candidates[0];
            }

            static nlohmann::json Vec4Json(const vec4 &v)
            {
                return nlohmann::json::array({v.x, v.y, v.z, v.w});
            }

            static std::string MakeNodeId(NodeId *node)
            {
                return "node:" + std::to_string(node->index) + ":" + std::to_string(node->revision);
            }

            static bool ReadFloatArray(const nlohmann::json &value, float *out, int count)
            {
                if (!value.is_array() || static_cast<int>(value.size()) != count)
                    return false;
                for (int i = 0; i < count; ++i)
                {
                    if (!value[i].is_number())
                        return false;
                    out[i] = value[i].get<float>();
                }
                return true;
            }

            static bool HasFloatArray(const nlohmann::json &args, const char *key, int count)
            {
                return args.contains(key) && args[key].is_array() && static_cast<int>(args[key].size()) == count;
            }

            static bool ReadIntArray(const nlohmann::json &value, int *out, int count)
            {
                if (!value.is_array() || static_cast<int>(value.size()) < count)
                    return false;
                for (int i = 0; i < count; ++i)
                {
                    if (!value[i].is_number_integer())
                        return false;
                    out[i] = value[i].get<int>();
                }
                return true;
            }

            static bool ReadSpriteFrameRectRaw(const nlohmann::json &frame, int rect[4])
            {
                return frame.is_object() && frame.contains("rect") && ReadIntArray(frame["rect"], rect, 4);
            }

            static std::string SpriteFrameName(const nlohmann::json &frame, int index)
            {
                if (frame.is_object() && frame.contains("name") && frame["name"].is_string() &&
                    !frame["name"].get<std::string>().empty())
                    return frame["name"].get<std::string>();
                return "frame_" + std::to_string(index);
            }

            static nlohmann::json SpriteFrameTimelineEntry(const nlohmann::json &frames, int index, float seconds)
            {
                nlohmann::json item;
                item["index"] = index;
                item["name"] = SpriteFrameName(frames[index], index);
                item["seconds"] = seconds;
                int rect[4]{};
                if (ReadSpriteFrameRectRaw(frames[index], rect))
                    item["rect"] = {rect[0], rect[1], rect[2], rect[3]};
                return item;
            }

            static std::filesystem::path ResolveSpriteMetadataImagePath(const std::filesystem::path &metadataPath,
                                                                         const std::string &imagePath)
            {
                if (imagePath.empty())
                    return {};

                std::filesystem::path p = U8Path(imagePath);
                if (p.is_absolute())
                    return p.lexically_normal();

                const std::filesystem::path metadataRelative = (metadataPath.parent_path() / p).lexically_normal();
                std::error_code ec;
                if (std::filesystem::exists(metadataRelative, ec))
                    return metadataRelative;

                return ResolveTexturePath(imagePath).lexically_normal();
            }

            static std::filesystem::path ResolveSpriteSheetImagePath(const std::filesystem::path &sheetPath,
                                                                      const std::string &imagePath)
            {
                std::filesystem::path p = U8Path(imagePath);
                if (p.is_absolute())
                    return p.lexically_normal();

                const std::filesystem::path sheetRelative = (sheetPath.parent_path() / p).lexically_normal();
                std::error_code ec;
                if (std::filesystem::exists(sheetRelative, ec))
                    return sheetRelative;

                return ResolveTexturePath(imagePath).lexically_normal();
            }
        } // namespace

        bool ApplySpriteJsonTransform(Scene &scene, NodeId *node, const nlohmann::json &args, int meshSlot,
                                      std::string &outError)
        {
            if (!node || !scene.IsNodeAlive(node))
            {
                outError = "node not found";
                return false;
            }

            const bool hasPos = HasFloatArray(args, "position", 3);
            const bool hasRot =
                HasFloatArray(args, "rotation", 3) || HasFloatArray(args, "rotation_euler_deg", 3);
            const bool hasScale = HasFloatArray(args, "scale", 3);
            const bool hasSize = HasFloatArray(args, "size", 2);
            if (!hasPos && !hasRot && !hasScale && !hasSize)
                return true;

            float pos[3]{}, rot[3]{}, scale[3]{}, size[2]{};
            if (hasPos && !ReadFloatArray(args["position"], pos, 3))
            {
                outError = "position must be [x,y,z]";
                return false;
            }
            if (hasRot)
            {
                const char *rotKey =
                    HasFloatArray(args, "rotation", 3) ? "rotation" : "rotation_euler_deg";
                if (!ReadFloatArray(args[rotKey], rot, 3))
                {
                    outError = "rotation must be [x,y,z] degrees";
                    return false;
                }
            }
            if (hasScale && !ReadFloatArray(args["scale"], scale, 3))
            {
                outError = "scale must be [x,y,z]";
                return false;
            }
            if (hasSize && !ReadFloatArray(args["size"], size, 2))
            {
                outError = "size must be [width,height]";
                return false;
            }

            mat4 local = scene.GetLocalMatrix(node);
            vec3 curScale(length(vec3(local[0])), length(vec3(local[1])), length(vec3(local[2])));
            float sx = curScale.x > 1e-6f ? curScale.x : 1.0f;
            float sy = curScale.y > 1e-6f ? curScale.y : 1.0f;
            float sz = curScale.z > 1e-6f ? curScale.z : 1.0f;
            mat3 rotMat(vec3(local[0]) / sx, vec3(local[1]) / sy, vec3(local[2]) / sz);

            vec3 newPos = hasPos ? vec3(pos[0], pos[1], pos[2]) : vec3(local[3]);
            vec3 newRot = hasRot ? vec3(glm::radians(rot[0]), glm::radians(rot[1]), glm::radians(rot[2]))
                                 : glm::eulerAngles(quat_cast(rotMat));
            vec3 newScale = curScale;

            if (hasSize)
            {
                const auto &refs = scene.GetMeshRefs(node);
                if (meshSlot < 0 || meshSlot >= static_cast<int>(refs.size()) ||
                    !scene.IsValidMeshIndex(refs[meshSlot]))
                {
                    outError = "size requires a valid sprite mesh slot";
                    return false;
                }

                const Mesh &mesh = scene.GetMesh(refs[meshSlot]);
                const vec3 extents = mesh.boundingBox.max - mesh.boundingBox.min;
                const float baseW = std::abs(extents.x) > 1e-6f ? std::abs(extents.x) : 1.0f;
                const float baseH = std::abs(extents.y) > 1e-6f ? std::abs(extents.y) : 1.0f;
                newScale.x = size[0] / baseW;
                newScale.y = size[1] / baseH;
            }
            if (hasScale)
                newScale = vec3(scale[0], scale[1], scale[2]);

            scene.SetLocalMatrix(
                node, glm::translate(mat4(1.f), newPos) * mat4_cast(quat(newRot)) * glm::scale(mat4(1.f), newScale));
            scene.MarkDirty();
            return true;
        }

        void ReadSpriteOptions(const nlohmann::json &args, Options &options)
        {
            std::string path = args.value("path", args.value("asset_path", ""));
            if (path.empty())
                path = args.value("metadata_path", args.value("image_path", ""));
            options.assetPath = U8Path(path);
            options.name = args.value("name", "");
            options.frameIndex = args.value("frame", args.value("frame_index", -1));
            options.frameName = args.value("frame_name", "");

            float tint[4]{};
            if (args.contains("tint") && ReadFloatArray(args["tint"], tint, 4))
            {
                options.hasTint = true;
                options.tint = vec4(tint[0], tint[1], tint[2], tint[3]);
            }

            float uv[4]{};
            if (args.contains("uv_rect") && ReadFloatArray(args["uv_rect"], uv, 4))
            {
                options.hasUvRect = true;
                options.uvRect = vec4(uv[0], uv[1], uv[2], uv[3]);
            }
        }

        nlohmann::json SpriteComponentJson(const NodeSpriteComponent &sprite)
        {
            nlohmann::json frames = nlohmann::json::array();
            for (int i = 0; i < static_cast<int>(sprite.frames.size()); ++i)
            {
                const NodeSpriteFrame &frame = sprite.frames[i];
                frames.push_back({
                    {"index", i},
                    {"name", frame.name},
                    {"rect", {frame.x, frame.y, frame.w, frame.h}},
                    {"pivot", {frame.pivotX, frame.pivotY}},
                    {"duration", frame.duration},
                    {"uv_rect", Vec4Json(frame.uvRect)},
                });
            }

            nlohmann::json clips = nlohmann::json::array();
            for (int i = 0; i < static_cast<int>(sprite.clips.size()); ++i)
            {
                const NodeSpriteClip &clip = sprite.clips[i];
                clips.push_back({
                    {"index", i},
                    {"name", clip.name},
                    {"start", clip.start},
                    {"end", clip.end},
                    {"fps", clip.fps},
                    {"loop", clip.loop},
                });
            }

            return {
                {"image_path", sprite.imagePath},
                {"metadata_path", sprite.metadataPath},
                {"frame_name", sprite.frameName},
                {"frame_index", sprite.frameIndex},
                {"image_size", {{"width", sprite.imageWidth}, {"height", sprite.imageHeight}}},
                {"frame_size", {{"width", sprite.frameWidth}, {"height", sprite.frameHeight}}},
                {"quad_size", {sprite.quadWidth, sprite.quadHeight}},
                {"uv_rect", Vec4Json(sprite.uvRect)},
                {"tint", Vec4Json(sprite.tint)},
                {"metadata_loaded", sprite.metadataLoaded},
                {"frame_count", sprite.frames.size()},
                {"clip_count", sprite.clips.size()},
                {"frames", std::move(frames)},
                {"clips", std::move(clips)},
                {"playback",
                 {
                     {"playing", sprite.playing},
                     {"active_clip", sprite.activeClipName},
                     {"active_clip_index", sprite.activeClipIndex},
                     {"mesh_slot", sprite.meshSlot},
                     {"loop", sprite.loop},
                     {"speed", sprite.playbackSpeed},
                     {"accumulator", sprite.playbackAccumulator},
                 }},
            };
        }

        nlohmann::json SpriteResultJson(Scene &scene, const Result &result)
        {
            nlohmann::json json;
            json["status"] = "ok";
            if (result.node)
            {
                json["node"] = MakeNodeId(result.node);
                json["name"] = scene.GetNodeName(result.node);
                json["index"] = result.node->index;
            }
            if (!result.imagePath.empty())
                json["image_path"] = PathUtf8(result.imagePath);
            if (!result.metadataPath.empty())
                json["metadata_path"] = PathUtf8(result.metadataPath);
            if (!result.frameName.empty())
                json["frame_name"] = result.frameName;
            json["frame_index"] = result.frameIndex;
            json["image_size"] = {{"width", result.imageWidth}, {"height", result.imageHeight}};
            json["frame_size"] = {{"width", result.frameWidth}, {"height", result.frameHeight}};
            json["quad_size"] = {result.quadWidth, result.quadHeight};
            json["uv_rect"] = {result.uvRect.x, result.uvRect.y, result.uvRect.z, result.uvRect.w};
            if (result.node)
            {
                if (const NodeSpriteComponent *sprite = scene.GetSpriteComponent(result.node))
                    json["sprite"] = SpriteComponentJson(*sprite);
            }
            return json;
        }

        std::filesystem::path ResolveSpriteMetadataPath(const std::string &path)
        {
            std::filesystem::path resolved = ResolveAssetPath(path);
            const std::string filename = ToLower(resolved.filename().string());
            if (filename.ends_with(".sprite.json"))
                return resolved;

            if (ToLower(resolved.extension().string()) == ".json")
            {
                std::filesystem::path renamed = resolved;
                renamed.replace_filename(resolved.stem().string() + ".sprite.json");
                return renamed;
            }

            resolved += ".sprite.json";
            return resolved;
        }

        std::filesystem::path ResolveSpriteSheetPath(const std::string &path)
        {
            std::filesystem::path resolved = ResolveAssetPath(path);
            const std::string filename = ToLower(resolved.filename().string());
            if (filename.ends_with(".sheet.json"))
                return resolved;

            if (ToLower(resolved.extension().string()) == ".json")
            {
                std::filesystem::path renamed = resolved;
                renamed.replace_filename(resolved.stem().string() + ".sheet.json");
                return renamed;
            }

            resolved += ".sheet.json";
            return resolved;
        }

        std::string AssetRelativePathForTool(const std::filesystem::path &path)
        {
            if (path.empty())
                return {};

            const std::filesystem::path assets = U8Path(Path::Assets);
            std::error_code ec;
            std::filesystem::path rel = std::filesystem::relative(path, assets, ec);
            if (!ec && !rel.empty() && rel.begin() != rel.end() && *rel.begin() != "..")
                return PathUtf8(rel);
            return PathUtf8(path);
        }

        bool ReadSpriteSheetImageEntry(const nlohmann::json &entry, nlohmann::json &outEntry, std::string &outError)
        {
            std::string imagePath;
            std::string label;
            const bool objectEntry = entry.is_object();
            if (entry.is_string())
            {
                imagePath = entry.get<std::string>();
            }
            else if (objectEntry && entry.contains("path") && entry["path"].is_string())
            {
                imagePath = entry["path"].get<std::string>();
                label = entry.value("label", "");
            }
            else
            {
                outError = "sheet image entries must be strings or objects with path";
                return false;
            }

            if (imagePath.empty())
            {
                outError = "sheet image path cannot be empty";
                return false;
            }

            const std::filesystem::path resolved = ResolveTexturePath(imagePath);
            if (!pmcp::IsPathSafe(resolved.string(), Path::Assets))
            {
                outError = "sheet image paths must stay inside Assets/";
                return false;
            }

            const std::string assetPath = AssetRelativePathForTool(resolved);
            if (objectEntry || !label.empty())
            {
                outEntry = nlohmann::json::object();
                outEntry["path"] = assetPath;
                if (!label.empty())
                    outEntry["label"] = label;
            }
            else
            {
                outEntry = assetPath;
            }
            return true;
        }

        nlohmann::json GenerateSpriteFramesJson(const nlohmann::json &grid, int imageW, int imageH)
        {
            nlohmann::json frames = nlohmann::json::array();
            if (!grid.is_object() || imageW <= 0 || imageH <= 0)
                return frames;

            const int frameW = std::max(1, grid.value("frame_width", 32));
            const int frameH = std::max(1, grid.value("frame_height", 32));
            const int marginX = std::max(0, grid.value("margin_x", 0));
            const int marginY = std::max(0, grid.value("margin_y", 0));
            const int spacingX = std::max(0, grid.value("spacing_x", 0));
            const int spacingY = std::max(0, grid.value("spacing_y", 0));
            const int maxFrames = std::max(0, grid.value("max_frames", 0));
            const float duration = grid.value("duration", 0.1f);

            int count = 0;
            for (int y = marginY; y + frameH <= imageH; y += frameH + spacingY)
            {
                for (int x = marginX; x + frameW <= imageW; x += frameW + spacingX)
                {
                    if (maxFrames > 0 && count >= maxFrames)
                        return frames;
                    char name[32];
                    std::snprintf(name, sizeof(name), "frame_%03d", count);
                    frames.push_back({
                        {"name", name},
                        {"rect", {x, y, frameW, frameH}},
                        {"pivot", {0.5f, 0.5f}},
                        {"duration", duration},
                    });
                    ++count;
                }
            }
            return frames;
        }

        bool LoadSpriteMetadataForEdit(const std::string &pathArg, std::filesystem::path &path,
                                       nlohmann::json &root, std::string &error)
        {
            if (pathArg.empty())
            {
                error = "path required";
                return false;
            }

            path = ResolveSpriteMetadataPath(pathArg);
            if (!pmcp::IsPathSafe(path.string(), Path::Assets))
            {
                error = "path must stay inside Assets/";
                return false;
            }

            std::ifstream in(path, std::ios::binary);
            if (!in)
            {
                error = "sprite metadata not found: " + pathArg;
                return false;
            }

            root = nlohmann::json::parse(in, nullptr, false);
            if (!root.is_object())
            {
                error = "invalid sprite metadata json";
                return false;
            }

            if (!root.contains("frames") || !root["frames"].is_array())
                root["frames"] = nlohmann::json::array();
            if (!root.contains("clips") || !root["clips"].is_array())
                root["clips"] = nlohmann::json::array();
            if (!root.contains("schema") || !root["schema"].is_string())
                root["schema"] = "phasma.sprite_editor.v1";
            return true;
        }

        bool SaveJsonAsset(const std::filesystem::path &path, const nlohmann::json &root, std::string &error)
        {
            std::error_code ec;
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path(), ec);
            if (ec)
            {
                error = "failed to create asset directory: " + ec.message();
                return false;
            }

            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                error = "failed to open asset for writing: " + PathUtf8(path);
                return false;
            }
            out << root.dump(2) << "\n";
            if (!out)
            {
                error = "failed to write asset: " + PathUtf8(path);
                return false;
            }
            return true;
        }

        nlohmann::json SpriteMetadataEditResult(const std::filesystem::path &path, const nlohmann::json &root)
        {
            return {
                {"status", "ok"},
                {"path", PathUtf8(path)},
                {"asset_path", AssetRelativePathForTool(path)},
                {"frame_count", root.value("frames", nlohmann::json::array()).size()},
                {"clip_count", root.value("clips", nlohmann::json::array()).size()},
                {"metadata", root},
            };
        }

        int FindNamedOrIndexedItem(const nlohmann::json &items, const nlohmann::json &args,
                                   std::initializer_list<const char *> indexKeys,
                                   std::initializer_list<const char *> nameKeys, std::string &error)
        {
            int index = -1;
            for (const char *key : indexKeys)
            {
                if (args.contains(key) && args[key].is_number_integer())
                {
                    index = args[key].get<int>();
                    break;
                }
            }

            if (index >= 0)
            {
                if (index >= static_cast<int>(items.size()))
                {
                    error = "index out of range";
                    return -1;
                }
                return index;
            }

            std::string name;
            for (const char *key : nameKeys)
            {
                if (args.contains(key) && args[key].is_string())
                {
                    name = args[key].get<std::string>();
                    break;
                }
            }

            if (name.empty())
            {
                error = "index or name required";
                return -1;
            }

            for (int i = 0; i < static_cast<int>(items.size()); ++i)
            {
                if (items[i].is_object() && items[i].value("name", "") == name)
                    return i;
            }

            error = "name not found: " + name;
            return -1;
        }

        bool ApplySpriteFrameFields(nlohmann::json &frame, const nlohmann::json &source, bool requireRect,
                                    std::string &error)
        {
            const nlohmann::json *src = &source;
            if (source.contains("frame") && source["frame"].is_object())
                src = &source["frame"];

            if (src->contains("name"))
            {
                if (!(*src)["name"].is_string())
                {
                    error = "frame name must be a string";
                    return false;
                }
                frame["name"] = (*src)["name"].get<std::string>();
            }

            int rect[4]{};
            const bool hasRect = src->contains("rect") && ReadIntArray((*src)["rect"], rect, 4);
            const bool hasLooseRect = src->contains("x") || src->contains("y") || src->contains("w") ||
                                      src->contains("h") || src->contains("width") || src->contains("height");
            if (hasRect)
            {
                frame["rect"] = {std::max(0, rect[0]), std::max(0, rect[1]), std::max(1, rect[2]),
                                 std::max(1, rect[3])};
            }
            else if (hasLooseRect)
            {
                auto existingRectValue = [&](int index, int fallback) -> int
                {
                    return frame.contains("rect") && frame["rect"].is_array() &&
                                   frame["rect"].size() > static_cast<size_t>(index) &&
                                   frame["rect"][index].is_number_integer()
                               ? frame["rect"][index].get<int>()
                               : fallback;
                };
                int x = existingRectValue(0, 0);
                int y = existingRectValue(1, 0);
                int w = existingRectValue(2, 1);
                int h = existingRectValue(3, 1);
                x = std::max(0, src->value("x", x));
                y = std::max(0, src->value("y", y));
                w = std::max(1, src->value("w", src->value("width", w)));
                h = std::max(1, src->value("h", src->value("height", h)));
                frame["rect"] = {x, y, w, h};
            }
            else if (requireRect)
            {
                error = "frame rect required";
                return false;
            }

            float pivot[2]{};
            if (src->contains("pivot"))
            {
                if (!ReadFloatArray((*src)["pivot"], pivot, 2))
                {
                    error = "pivot must be [x,y]";
                    return false;
                }
                frame["pivot"] = {std::clamp(pivot[0], 0.0f, 1.0f), std::clamp(pivot[1], 0.0f, 1.0f)};
            }

            if (src->contains("duration"))
            {
                if (!(*src)["duration"].is_number())
                {
                    error = "duration must be a number";
                    return false;
                }
                frame["duration"] = std::max(0.01f, (*src)["duration"].get<float>());
            }

            if (src->contains("hitbox"))
            {
                if (!(*src)["hitbox"].is_object())
                {
                    error = "hitbox must be an object";
                    return false;
                }
                frame["hitbox"] = (*src)["hitbox"];
            }
            else if (src->contains("hitbox_rect"))
            {
                int hitbox[4]{};
                if (!ReadIntArray((*src)["hitbox_rect"], hitbox, 4))
                {
                    error = "hitbox_rect must be [x,y,w,h]";
                    return false;
                }
                frame["hitbox"] = {
                    {"enabled", true},
                    {"rect", {hitbox[0], hitbox[1], std::max(1, hitbox[2]), std::max(1, hitbox[3])}},
                };
            }

            if (!frame.contains("name") || !frame["name"].is_string() ||
                frame["name"].get<std::string>().empty())
                frame["name"] = "frame";
            if (!frame.contains("pivot"))
                frame["pivot"] = {0.5f, 0.5f};
            if (!frame.contains("duration"))
                frame["duration"] = 0.1f;
            return true;
        }

        bool ApplySpriteClipFields(nlohmann::json &clip, const nlohmann::json &source, bool requireRange,
                                   std::string &error)
        {
            const nlohmann::json *src = &source;
            if (source.contains("clip") && source["clip"].is_object())
                src = &source["clip"];

            if (src->contains("name"))
            {
                if (!(*src)["name"].is_string())
                {
                    error = "clip name must be a string";
                    return false;
                }
                clip["name"] = (*src)["name"].get<std::string>();
            }
            if (src->contains("start"))
            {
                if (!(*src)["start"].is_number_integer())
                {
                    error = "clip start must be an integer";
                    return false;
                }
                clip["start"] = std::max(0, (*src)["start"].get<int>());
            }
            if (src->contains("end"))
            {
                if (!(*src)["end"].is_number_integer())
                {
                    error = "clip end must be an integer";
                    return false;
                }
                clip["end"] = std::max(0, (*src)["end"].get<int>());
            }
            if (requireRange && (!clip.contains("start") || !clip.contains("end")))
            {
                error = "clip start and end required";
                return false;
            }
            if (clip.contains("start") && clip.contains("end") &&
                clip["start"].get<int>() > clip["end"].get<int>())
                std::swap(clip["start"], clip["end"]);

            if (src->contains("fps"))
            {
                if (!(*src)["fps"].is_number())
                {
                    error = "clip fps must be a number";
                    return false;
                }
                clip["fps"] = std::max(0.1f, (*src)["fps"].get<float>());
            }
            if (src->contains("loop"))
            {
                if (!(*src)["loop"].is_boolean())
                {
                    error = "clip loop must be a boolean";
                    return false;
                }
                clip["loop"] = (*src)["loop"].get<bool>();
            }

            if (!clip.contains("name") || !clip["name"].is_string() ||
                clip["name"].get<std::string>().empty())
                clip["name"] = "clip";
            if (!clip.contains("fps"))
                clip["fps"] = 10.0f;
            if (!clip.contains("loop"))
                clip["loop"] = true;
            return true;
        }

        void ClampSpriteClips(nlohmann::json &root)
        {
            if (!root.contains("clips") || !root["clips"].is_array())
                return;

            const int maxFrame = root.contains("frames") && root["frames"].is_array()
                                     ? std::max(0, static_cast<int>(root["frames"].size()) - 1)
                                     : 0;
            for (auto &clip : root["clips"])
            {
                if (!clip.is_object())
                    continue;
                int start = clip.contains("start") && clip["start"].is_number_integer()
                                ? clip["start"].get<int>()
                                : 0;
                int end =
                    clip.contains("end") && clip["end"].is_number_integer() ? clip["end"].get<int>() : start;
                start = std::clamp(start, 0, maxFrame);
                end = std::clamp(end, 0, maxFrame);
                if (start > end)
                    std::swap(start, end);
                clip["start"] = start;
                clip["end"] = end;
                const float fps =
                    clip.contains("fps") && clip["fps"].is_number() ? clip["fps"].get<float>() : 10.0f;
                clip["fps"] = std::max(0.1f, fps);
                if (!clip.contains("loop") || !clip["loop"].is_boolean())
                    clip["loop"] = true;
            }
        }

        nlohmann::json ValidateSpriteMetadataAsset(const std::string &pathArg)
        {
            nlohmann::json result;
            result["status"] = "ok";
            result["valid"] = false;
            result["errors"] = nlohmann::json::array();
            result["warnings"] = nlohmann::json::array();
            result["frames"] = nlohmann::json::array();
            result["clips"] = nlohmann::json::array();
            result["clip_timelines"] = nlohmann::json::array();

            auto addError = [&](const std::string &message) { result["errors"].push_back(message); };
            auto addWarning = [&](const std::string &message) { result["warnings"].push_back(message); };

            if (pathArg.empty())
            {
                addError("path required");
                result["error_count"] = result["errors"].size();
                result["warning_count"] = result["warnings"].size();
                return result;
            }

            const std::filesystem::path path = ResolveSpriteMetadataPath(pathArg);
            result["path"] = PathUtf8(path);
            result["asset_path"] = AssetRelativePathForTool(path);
            if (!pmcp::IsPathSafe(path.string(), Path::Assets))
            {
                addError("path must stay inside Assets/");
                result["error_count"] = result["errors"].size();
                result["warning_count"] = result["warnings"].size();
                return result;
            }

            std::ifstream in(path, std::ios::binary);
            if (!in)
            {
                addError("sprite metadata not found: " + pathArg);
                result["error_count"] = result["errors"].size();
                result["warning_count"] = result["warnings"].size();
                return result;
            }

            nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
            if (!root.is_object())
            {
                addError("invalid sprite metadata json");
                result["error_count"] = result["errors"].size();
                result["warning_count"] = result["warnings"].size();
                return result;
            }

            result["schema"] = root.contains("schema") && root["schema"].is_string()
                                    ? root["schema"].get<std::string>()
                                    : "";
            std::string imageValue;
            if (root.contains("image"))
            {
                if (root["image"].is_string())
                    imageValue = root["image"].get<std::string>();
                else
                    addError("metadata image path must be a string");
            }
            result["image"] = imageValue;

            int actualW = 0;
            int actualH = 0;
            if (imageValue.empty())
            {
                addError("metadata image path is empty");
            }
            else
            {
                const std::filesystem::path imagePath =
                    ResolveSpriteMetadataImagePath(path, imageValue);
                result["resolved_image"] = PathUtf8(imagePath);
                if (!pmcp::IsPathSafe(imagePath.string(), Path::Assets))
                    addWarning("resolved image is outside Assets/");

                std::error_code ec;
                if (!std::filesystem::exists(imagePath, ec))
                {
                    addError("image not found: " + imageValue);
                }
                else
                {
                    int channels = 0;
                    if (!stbi_info(PathUtf8(imagePath).c_str(), &actualW, &actualH, &channels))
                    {
                        addError("failed to read image dimensions: " + imageValue);
                    }
                    else
                    {
                        result["actual_image_size"] = {
                            {"width", actualW}, {"height", actualH}, {"channels", channels}};
                    }
                }
            }

            int metaW = 0;
            int metaH = 0;
            if (root.contains("image_size") && root["image_size"].is_object())
            {
                const auto &imageSize = root["image_size"];
                if (imageSize.contains("width") && imageSize["width"].is_number_integer())
                    metaW = imageSize["width"].get<int>();
                else
                    addError("metadata image_size.width must be an integer");
                if (imageSize.contains("height") && imageSize["height"].is_number_integer())
                    metaH = imageSize["height"].get<int>();
                else
                    addError("metadata image_size.height must be an integer");
                result["metadata_image_size"] = {{"width", metaW}, {"height", metaH}};
                if (actualW > 0 && actualH > 0 && (metaW != actualW || metaH != actualH))
                    addError("metadata image_size does not match actual image dimensions");
            }
            else
            {
                addWarning("metadata image_size is missing");
            }

            const nlohmann::json frames = root.contains("frames") && root["frames"].is_array()
                                              ? root["frames"]
                                              : nlohmann::json::array();
            if (!root.contains("frames") || !root["frames"].is_array())
                addError("frames array is missing");
            if (frames.empty())
                addError("frames array is empty");

            std::unordered_map<std::string, int> frameNameCounts;
            for (int i = 0; i < static_cast<int>(frames.size()); ++i)
            {
                nlohmann::json frameInfo;
                frameInfo["index"] = i;
                frameInfo["valid"] = true;
                frameInfo["name"] = SpriteFrameName(frames[i], i);
                frameNameCounts[frameInfo["name"].get<std::string>()]++;

                int rect[4]{};
                if (!ReadSpriteFrameRectRaw(frames[i], rect))
                {
                    frameInfo["valid"] = false;
                    addError("frame " + std::to_string(i) + " has no valid rect [x,y,w,h]");
                }
                else
                {
                    frameInfo["rect"] = {rect[0], rect[1], rect[2], rect[3]};
                    if (rect[0] < 0 || rect[1] < 0 || rect[2] <= 0 || rect[3] <= 0)
                    {
                        frameInfo["valid"] = false;
                        addError("frame " + std::to_string(i) +
                                 " rect must have non-negative origin and positive size");
                    }
                    if (actualW > 0 && actualH > 0 &&
                        (rect[0] + rect[2] > actualW || rect[1] + rect[3] > actualH))
                    {
                        frameInfo["valid"] = false;
                        addError("frame " + std::to_string(i) + " rect is outside image bounds");
                    }
                }

                if (frames[i].is_object() && frames[i].contains("pivot"))
                {
                    const auto &pivot = frames[i]["pivot"];
                    if (!pivot.is_array() || pivot.size() < 2 || !pivot[0].is_number() ||
                        !pivot[1].is_number())
                    {
                        frameInfo["valid"] = false;
                        addError("frame " + std::to_string(i) + " pivot must be [x,y]");
                    }
                }
                if (frames[i].is_object() && frames[i].contains("duration") &&
                    (!frames[i]["duration"].is_number() || frames[i]["duration"].get<float>() <= 0.0f))
                {
                    frameInfo["valid"] = false;
                    addError("frame " + std::to_string(i) + " duration must be positive");
                }

                if (frames[i].is_object() && frames[i].contains("hitbox"))
                {
                    const auto &hitbox = frames[i]["hitbox"];
                    if (!hitbox.is_object())
                    {
                        frameInfo["valid"] = false;
                        addError("frame " + std::to_string(i) + " hitbox must be an object");
                    }
                    else if (hitbox.contains("rect"))
                    {
                        int hitboxRect[4]{};
                        if (!ReadIntArray(hitbox["rect"], hitboxRect, 4) || hitboxRect[2] <= 0 ||
                            hitboxRect[3] <= 0)
                        {
                            frameInfo["valid"] = false;
                            addError("frame " + std::to_string(i) +
                                     " hitbox rect must be [x,y,w,h] with positive size");
                        }
                    }
                }

                result["frames"].push_back(std::move(frameInfo));
            }
            for (const auto &[name, count] : frameNameCounts)
            {
                if (count > 1)
                    addWarning("duplicate frame name: " + name);
            }

            const nlohmann::json clips = root.contains("clips") && root["clips"].is_array()
                                             ? root["clips"]
                                             : nlohmann::json::array();
            if (!root.contains("clips") || !root["clips"].is_array())
                addWarning("clips array is missing");
            if (clips.empty())
                addWarning("clips array is empty");

            const int frameCount = static_cast<int>(frames.size());
            for (int i = 0; i < static_cast<int>(clips.size()); ++i)
            {
                nlohmann::json clipInfo;
                clipInfo["index"] = i;
                clipInfo["valid"] = true;

                if (!clips[i].is_object())
                {
                    clipInfo["valid"] = false;
                    addError("clip " + std::to_string(i) + " must be an object");
                    result["clips"].push_back(std::move(clipInfo));
                    continue;
                }

                const auto &clip = clips[i];
                const std::string clipName =
                    clip.contains("name") && clip["name"].is_string() &&
                            !clip["name"].get<std::string>().empty()
                        ? clip["name"].get<std::string>()
                        : "clip_" + std::to_string(i);
                clipInfo["name"] = clipName;
                const bool hasStart = clip.contains("start") && clip["start"].is_number_integer();
                const bool hasEnd = clip.contains("end") && clip["end"].is_number_integer();
                int start = hasStart ? clip["start"].get<int>() : 0;
                int end = hasEnd ? clip["end"].get<int>() : start;
                if (!hasStart || !hasEnd)
                {
                    clipInfo["valid"] = false;
                    addError("clip " + std::to_string(i) + " requires integer start and end");
                }
                if (start > end)
                {
                    clipInfo["valid"] = false;
                    addError("clip " + clipName + " start is greater than end");
                }
                if (frameCount == 0 || start < 0 || end < 0 || start >= frameCount || end >= frameCount)
                {
                    clipInfo["valid"] = false;
                    addError("clip " + clipName + " frame range is outside frame bounds");
                }

                float fps = 10.0f;
                if (clip.contains("fps") && clip["fps"].is_number())
                    fps = clip["fps"].get<float>();
                if (fps <= 0.0f)
                {
                    clipInfo["valid"] = false;
                    addError("clip " + clipName + " fps must be positive");
                    fps = 10.0f;
                }
                else if (clip.contains("fps") && !clip["fps"].is_number())
                {
                    clipInfo["valid"] = false;
                    addError("clip " + clipName + " fps must be a number");
                }
                bool loop = true;
                if (clip.contains("loop") && !clip["loop"].is_boolean())
                {
                    clipInfo["valid"] = false;
                    addError("clip " + clipName + " loop must be boolean");
                }
                else if (clip.contains("loop"))
                {
                    loop = clip["loop"].get<bool>();
                }

                clipInfo["start"] = start;
                clipInfo["end"] = end;
                clipInfo["fps"] = fps;
                clipInfo["loop"] = loop;
                result["clips"].push_back(clipInfo);

                if (frameCount > 0)
                {
                    const int clampedStart = std::clamp(start, 0, frameCount - 1);
                    const int clampedEnd = std::clamp(end, 0, frameCount - 1);
                    if (clampedStart <= clampedEnd)
                    {
                        nlohmann::json timeline;
                        timeline["clip_index"] = i;
                        timeline["name"] = clipName;
                        timeline["start"] = clampedStart;
                        timeline["end"] = clampedEnd;
                        timeline["fps"] = fps;
                        timeline["loop"] = loop;
                        timeline["frame_count"] = clampedEnd - clampedStart + 1;
                        timeline["total_seconds"] =
                            static_cast<float>(clampedEnd - clampedStart + 1) / std::max(0.1f, fps);
                        timeline["frames"] = nlohmann::json::array();
                        for (int frameIndex = clampedStart; frameIndex <= clampedEnd; ++frameIndex)
                            timeline["frames"].push_back(
                                SpriteFrameTimelineEntry(frames, frameIndex, 1.0f / std::max(0.1f, fps)));
                        result["clip_timelines"].push_back(std::move(timeline));
                    }
                }
            }

            result["frame_count"] = frames.size();
            result["clip_count"] = clips.size();
            result["valid"] = result["errors"].empty();
            result["error_count"] = result["errors"].size();
            result["warning_count"] = result["warnings"].size();
            return result;
        }

        bool ReadSpriteSheetImageAt(const nlohmann::json &sheet, const std::filesystem::path &sheetPath,
                                    int index, std::filesystem::path &outImage, std::string &outLabel,
                                    std::string &error)
        {
            if (!sheet.contains("images") || !sheet["images"].is_array() || sheet["images"].empty())
            {
                error = "sheet asset has no images";
                return false;
            }

            if (index < 0 || index >= static_cast<int>(sheet["images"].size()))
            {
                error = "image_index out of range";
                return false;
            }

            const nlohmann::json &entry = sheet["images"][index];
            std::string imagePath;
            if (entry.is_string())
            {
                imagePath = entry.get<std::string>();
            }
            else if (entry.is_object() && entry.contains("path") && entry["path"].is_string())
            {
                imagePath = entry["path"].get<std::string>();
                outLabel = entry.value("label", "");
            }
            else
            {
                error = "sheet image entry must be a string or object with path";
                return false;
            }

            outImage = ResolveSpriteSheetImagePath(sheetPath, imagePath);
            if (!pmcp::IsPathSafe(outImage.string(), Path::Assets))
            {
                error = "sheet image must stay inside Assets/";
                return false;
            }
            return true;
        }
    } // namespace SpriteAuthoring
} // namespace pe
