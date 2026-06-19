#pragma once
#include "Base/Math.h"

namespace pe
{
    class Scene;
    struct NodeId;

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
    } // namespace SpriteAuthoring
} // namespace pe
