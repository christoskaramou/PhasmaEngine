#include "GUIState.h"
#include "Base/Process.h"

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

namespace
{
    // "open" would run these instead of viewing them. Reveal in File Manager still reaches them.
    bool RefusesToOpen(const std::filesystem::path &path)
    {
#if defined(_WIN32)
        std::string ext = pe::PathUtf8(path.extension());
        for (char &c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        static constexpr const char *kRunsOnOpen[] = {".exe", ".com", ".scr", ".pif", ".bat", ".cmd", ".ps1",
                                                      ".vbs", ".vbe", ".js", ".jse", ".wsf", ".wsh", ".msc",
                                                      ".msi", ".msp", ".hta", ".cpl", ".lnk", ".url", ".reg"};
        for (const char *runs : kRunsOnOpen)
            if (ext == runs)
                return true;
        return false;
#else
        return path.extension() == ".desktop" ||
               (std::filesystem::is_regular_file(path) && access(path.c_str(), X_OK) == 0);
#endif
    }
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

        // Only an existing path, made canonical: nothing relative and nothing that is not on disk.
        std::error_code ec;
        const std::filesystem::path canonical =
            std::filesystem::canonical(std::filesystem::path(reinterpret_cast<const char8_t *>(absPath.c_str())), ec);
        if (ec)
            return;
        if (RefusesToOpen(canonical))
        {
            PE_WARN("[Editor] Not opening executable content %s; use Reveal in File Manager", PathUtf8(canonical).c_str());
            return;
        }

        ThreadPool::GUI.Enqueue([canonical]()
                                {
#if defined(_WIN32)
                                    // Verb "open" on one canonical, existing, non-executable file or folder.
                                    // nosec
                                    ShellExecuteW(nullptr, L"open", canonical.wstring().c_str(), nullptr, nullptr, SW_SHOW);
#elif defined(__APPLE__)
                                    RunProcess("open", {PathUtf8(canonical)});
#else
                                    RunProcess("xdg-open", {PathUtf8(canonical)});
#endif
                                });
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

        switch (Settings::Get<SceneSettings>().scene_view_aspect_mode)
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
