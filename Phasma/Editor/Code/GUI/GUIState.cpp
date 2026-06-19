#include "GUIState.h"

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

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

        // Only open paths that exist on disk to prevent command injection
        if (!std::filesystem::exists(path))
            return;

        // Resolve to a canonical absolute path to prevent path traversal
        std::error_code ec;
        auto canonical = std::filesystem::canonical(path, ec);
        if (ec)
            return;

        std::string safePath = canonical.string();

#if defined(__APPLE__)
        const char *opener = "open";
#else
        const char *opener = "xdg-open";
#endif

        pid_t pid = fork();
        if (pid == 0)
        {
            execlp(opener, opener, safePath.c_str(), static_cast<char *>(nullptr));
            _exit(127);
        }
        if (pid > 0)
        {
            int status = 0;
            waitpid(pid, &status, 0);
        }
    }
#endif
} // namespace

namespace pe
{
    AssetPreviewState GUIState::s_assetPreview{};
    std::atomic_bool GUIState::s_modelLoading{false};

    void *GUIState::s_viewportTextureId = nullptr;
    bool GUIState::s_sceneViewFloating = false;
    bool GUIState::s_sceneViewRedockQueued = false;
    bool GUIState::s_sceneViewFocused = false;
    bool GUIState::s_hierarchyFocused = false;
    Image *GUIState::s_sceneViewImage = nullptr;
    bool GUIState::s_sceneViewImageRectValid = false;
    float GUIState::s_sceneViewImageMinX = 0.0f;
    float GUIState::s_sceneViewImageMinY = 0.0f;
    float GUIState::s_sceneViewImageAbsMinX = 0.0f;
    float GUIState::s_sceneViewImageAbsMinY = 0.0f;
    float GUIState::s_sceneViewImageWidth = 0.0f;
    float GUIState::s_sceneViewImageHeight = 0.0f;
    bool GUIState::s_useTransformGizmo = true;
    bool GUIState::s_useLightGizmos = true;
    bool GUIState::s_useCameraGizmos = true;
    bool GUIState::s_useOrientationGizmo = true;
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

        // Only open paths that exist on disk to prevent command injection
        if (!std::filesystem::exists(absPath))
            return;

        // Resolve to canonical absolute path to prevent path traversal
        std::error_code ec;
        auto canonical = std::filesystem::canonical(absPath, ec);
        if (ec)
            return;

#if defined(_WIN32)
        std::string pathCopy = canonical.string();
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

    float GUIState::GetSceneViewAspectRatio(float fallbackAspect)
    {
        if (!std::isfinite(fallbackAspect) || fallbackAspect <= 0.0f)
            fallbackAspect = 16.0f / 9.0f;

        switch (Settings::Get<GlobalSettings>().scene_view_aspect_mode)
        {
        case SceneViewAspectMode::Landscape16x9:
            return 16.0f / 9.0f;
        case SceneViewAspectMode::Portrait9x16:
            return 9.0f / 16.0f;
        case SceneViewAspectMode::Landscape19_5x9:
            return 19.5f / 9.0f;
        case SceneViewAspectMode::Portrait9x19_5:
            return 9.0f / 19.5f;
        case SceneViewAspectMode::Square1x1:
            return 1.0f;
        case SceneViewAspectMode::Free:
        default:
            return fallbackAspect;
        }
    }
} // namespace pe
