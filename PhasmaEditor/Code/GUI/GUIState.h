#pragma once

namespace pe
{
    enum class AssetPreviewType
    {
        None,
        Model,
        Script,
        Shader
    };

    struct AssetPreviewState
    {
        AssetPreviewType type = AssetPreviewType::None;
        std::string label{};
        std::string fullPath{};
    };

    class GUIState
    {
    public:
        static AssetPreviewState s_assetPreview;
        static std::atomic_bool s_modelLoading;

        // SceneView State
        static void *s_viewportTextureId;
        static bool s_sceneViewFloating;
        static bool s_sceneViewRedockQueued;
        static Image *s_sceneViewImage;

        static void OpenExternalPath(const std::string &absPath);
        static void UpdateAssetPreview(AssetPreviewType type, const std::string &label, const std::string &fullPath);
    };
} // namespace pe
