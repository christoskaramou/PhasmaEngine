#include "GUIState.h"

#include <filesystem>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
#if !defined(_WIN32)
    void OpenExternalPathPosix(const std::string &path)
    {
        if (path.empty())
            return;

#if defined(__APPLE__)
        const char *opener = "open";
#else
        const char *opener = "xdg-open";
#endif

        pid_t pid = fork();
        if (pid == 0)
        {
            execlp(opener, opener, path.c_str(), static_cast<char *>(nullptr));
            _exit(127);
        }
        if (pid > 0)
        {
            int status = 0;
            waitpid(pid, &status, 0);
        }
    }
#endif
}

namespace pe
{
    AssetPreviewState GUIState::s_assetPreview{};
    std::atomic_bool GUIState::s_modelLoading{false};

    void *GUIState::s_viewportTextureId = nullptr;
    bool GUIState::s_sceneViewFloating = false;
    bool GUIState::s_sceneViewRedockQueued = false;
    bool GUIState::s_sceneViewFocused = false;
    Image *GUIState::s_sceneViewImage = nullptr;
    bool GUIState::s_useTransformGizmo = true;
    bool GUIState::s_useLightGizmos = true;
    bool GUIState::s_useCameraGizmos = true;
    bool GUIState::s_playMode = false;
    bool GUIState::s_isPaused = false;
    GUIStyle GUIState::s_guiStyle = GUIStyle::Unity; // Default to Unity style
    ImFont *GUIState::s_fontClassic = nullptr;
    ImFont *GUIState::s_fontUnity = nullptr;
    ImFont *GUIState::s_fontUnreal = nullptr;
    ImFont *GUIState::s_fontModern = nullptr;
    ImFont *GUIState::s_fontDark = nullptr;
    ImFont *GUIState::s_fontLight = nullptr;

    void GUIState::OpenExternalPath(const std::string &absPath)
    {
        if (absPath.empty())
            return;

#if defined(_WIN32)
        std::string pathCopy = absPath;
        ThreadPool::GUI.Enqueue([pathCopy]()
                                {
                                    std::filesystem::path path(reinterpret_cast<const char8_t *>(pathCopy.c_str()));
                                    ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOW); });
#else
        std::string pathCopy = absPath;
        ThreadPool::GUI.Enqueue([pathCopy]()
                                { OpenExternalPathPosix(pathCopy); });
#endif
    }

    void GUIState::UpdateAssetPreview(AssetPreviewType type, const std::string &label, const std::string &fullPath)
    {
        s_assetPreview.type = type;
        s_assetPreview.label = label;
        s_assetPreview.fullPath = fullPath;
    }
} // namespace pe
