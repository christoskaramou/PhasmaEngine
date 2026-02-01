#include "GUIState.h"

namespace pe
{
    AssetPreviewState GUIState::s_assetPreview{};
    std::atomic_bool GUIState::s_modelLoading{false};

    void *GUIState::s_viewportTextureId = nullptr;
    bool GUIState::s_sceneViewFloating = false;
    bool GUIState::s_sceneViewRedockQueued = false;
    Image *GUIState::s_sceneViewImage = nullptr;
    GUIStyle GUIState::s_guiStyle = GUIStyle::Unity; // Default to Unity style
    ImFont *GUIState::s_fontClassic = nullptr;
    ImFont *GUIState::s_fontUnity = nullptr;
    ImFont *GUIState::s_fontUnreal = nullptr;

    void GUIState::OpenExternalPath(const std::string &absPath)
    {
#if defined(_WIN32)
        std::string pathCopy = absPath;
        ThreadPool::GUI.Enqueue([pathCopy]()
                                {
                                    std::filesystem::path path(reinterpret_cast<const char8_t *>(pathCopy.c_str()));
                                    ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOW); });
#elif defined(__APPLE__)
        std::string command = "open \"" + absPath + "\"";
        ThreadPool::GUI.Enqueue([command]()
                                { system(command.c_str()); });
#else
        std::string command = "xdg-open \"" + absPath + "\"";
        ThreadPool::GUI.Enqueue([command]()
                                { system(command.c_str()); });
#endif
    }

    void GUIState::UpdateAssetPreview(AssetPreviewType type, const std::string &label, const std::string &fullPath)
    {
        s_assetPreview.type = type;
        s_assetPreview.label = label;
        s_assetPreview.fullPath = fullPath;
    }
} // namespace pe
