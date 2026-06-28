#pragma once

#include <nlohmann/json.hpp>

namespace pe
{
    class Scene;
    struct NodeId;
    class NodeSpriteComponent;

    namespace SpriteAuthoring
    {
        struct Options
        {
            std::filesystem::path assetPath;
            std::string name;
            int frameIndex = -1;
            std::string frameName;
            bool hasTint = false;
            vec4 tint = vec4(1.0f);
            bool hasUvRect = false;
            vec4 uvRect = vec4(0.0f, 0.0f, 1.0f, 1.0f);
        };

        struct Result
        {
            NodeId *node = nullptr;
            std::filesystem::path imagePath;
            std::filesystem::path metadataPath;
            std::string frameName;
            int frameIndex = -1;
            int imageWidth = 0;
            int imageHeight = 0;
            int frameWidth = 0;
            int frameHeight = 0;
            float quadWidth = 1.0f;
            float quadHeight = 1.0f;
            vec4 uvRect = vec4(0.0f, 0.0f, 1.0f, 1.0f);
            std::string error;
        };

        bool IsSpriteAssetPath(const std::filesystem::path &path);
        Result CreateNode(Scene &scene, const Options &options, NodeId *parent = nullptr);
        Result ApplyToNode(Scene &scene, NodeId *node, const Options &options, int meshSlot = 0);
        bool ApplyUvRect(Scene &scene, NodeId *node, const vec4 &uvRect, int meshSlot, std::string &outError);

        // --- JSON / MCP metadata helpers ------------------------------------

        void ReadSpriteOptions(const nlohmann::json &args, Options &options);
        bool ApplySpriteJsonTransform(Scene &scene, NodeId *node, const nlohmann::json &args, int meshSlot,
                                      std::string &outError);
        nlohmann::json SpriteComponentJson(const NodeSpriteComponent &sprite);
        nlohmann::json SpriteResultJson(Scene &scene, const Result &result);

        std::filesystem::path ResolveSpriteMetadataPath(const std::string &path);
        std::filesystem::path ResolveSpriteSheetPath(const std::string &path);
        std::string AssetRelativePathForTool(const std::filesystem::path &path);

        bool LoadSpriteMetadataForEdit(const std::string &pathArg, std::filesystem::path &path,
                                       nlohmann::json &root, std::string &error);
        bool SaveJsonAsset(const std::filesystem::path &path, const nlohmann::json &root,
                           std::string &error);
        nlohmann::json SpriteMetadataEditResult(const std::filesystem::path &path,
                                                const nlohmann::json &root);
        nlohmann::json ValidateSpriteMetadataAsset(const std::string &pathArg);

        nlohmann::json GenerateSpriteFramesJson(const nlohmann::json &grid, int imageW, int imageH);
        bool ReadSpriteSheetImageEntry(const nlohmann::json &entry, nlohmann::json &outEntry,
                                       std::string &outError);
        bool ReadSpriteSheetImageAt(const nlohmann::json &sheet, const std::filesystem::path &sheetPath,
                                    int index, std::filesystem::path &outImage, std::string &outLabel,
                                    std::string &error);

        bool ApplySpriteFrameFields(nlohmann::json &frame, const nlohmann::json &source, bool requireRect,
                                    std::string &error);
        bool ApplySpriteClipFields(nlohmann::json &clip, const nlohmann::json &source, bool requireRange,
                                   std::string &error);
        void ClampSpriteClips(nlohmann::json &root);
        int FindNamedOrIndexedItem(const nlohmann::json &items, const nlohmann::json &args,
                                   std::initializer_list<const char *> indexKeys,
                                   std::initializer_list<const char *> nameKeys, std::string &error);
    } // namespace SpriteAuthoring
} // namespace pe
