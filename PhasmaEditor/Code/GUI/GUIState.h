#pragma once

struct ImFont;

namespace pe
{
    enum class AssetPreviewType
    {
        None,
        Model,
        Script,
        Shader
    };

    enum class GUIStyle
    {
        Classic,   // Original PhasmaEngine style
        Unity,     // Unity-inspired dark theme
        Unreal     // Unreal Engine-inspired dark theme
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

        // GUI Style
        static GUIStyle s_guiStyle;

        // Style-specific fonts (loaded at init, switched dynamically)
        static ImFont* s_fontClassic;
        static ImFont* s_fontUnity;
        static ImFont* s_fontUnreal;

        static void OpenExternalPath(const std::string &absPath);
        static void UpdateAssetPreview(AssetPreviewType type, const std::string &label, const std::string &fullPath);
    };
} // namespace pe
