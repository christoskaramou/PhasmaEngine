#include "Runtime/RuntimeStartup.h"

#include "Project/Detail/ProjectHelpers.h"
#include "Project/ProjectPaths.h"

#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"

namespace pe
{
    namespace
    {
        constexpr float kMinRenderScale = 0.1f;
        constexpr float kMaxRenderScale = 4.0f;

        std::string PrefixStartupWarning(const char *source, const std::string &warning)
        {
            if (warning.empty())
                return {};
            return std::string("[") + source + "] " + warning;
        }
    } // namespace

    const char *RuntimeStartupSceneSourceName(RuntimeStartupSceneSource source)
    {
        switch (source)
        {
        case RuntimeStartupSceneSource::RuntimeSettings:
            return "runtime settings";
        case RuntimeStartupSceneSource::EditorConfig:
            return "editor restore";
        case RuntimeStartupSceneSource::ProjectManifest:
            return "project manifest";
        case RuntimeStartupSceneSource::None:
        default:
            return "none";
        }
    }

    std::filesystem::path RuntimeEditorConfigPath(const std::filesystem::path &editorConfigPath)
    {
        if (!editorConfigPath.empty())
            return project_detail::NormalizeAbsolute(editorConfigPath);

        Path::Init();
        const std::filesystem::path executableConfig =
            project_detail::Normalize(std::filesystem::path(Path::Executable) / kEditorConfigRelativePath);
        if (std::filesystem::exists(executableConfig))
            return executableConfig;

        return project_detail::Normalize(kEditorConfigRelativePath);
    }

    std::filesystem::path RuntimeEditorConfigWritePath(const std::filesystem::path &editorConfigPath)
    {
        if (!editorConfigPath.empty())
            return project_detail::NormalizeAbsolute(editorConfigPath);

        Path::Init();
        return project_detail::Normalize(std::filesystem::path(Path::Executable) / kEditorConfigRelativePath);
    }

    std::string ReadEditorStartupScene(const std::filesystem::path &editorConfigPath, std::string *warning)
    {
        rapidjson::Document document;
        std::string loadWarning;
        project_detail::TryLoadJsonObject(RuntimeEditorConfigPath(editorConfigPath), document, loadWarning);
        if (warning)
            *warning = loadWarning;

        return project_detail::ReadJsonStringField(document, kEditorLastSceneKey);
    }

    bool WriteEditorStartupScene(const std::filesystem::path &editorConfigPath,
                                 const std::string &startupScene,
                                 std::string *error)
    {
        const std::filesystem::path path = RuntimeEditorConfigWritePath(editorConfigPath);
        rapidjson::Document document;
        std::string warning;
        project_detail::TryLoadJsonObject(path, document, warning);

        project_detail::SetJsonStringMember(document, kEditorLastSceneKey, startupScene);
        return project_detail::WriteJsonObject(path, document, error);
    }

    RuntimeStartupSceneSelection ResolveRuntimeStartupScene(const ProjectSelection &projectSelection,
                                                            const RuntimeStartupSceneResolveOptions &options)
    {
        RuntimeStartupSceneSelection selection;

        std::string explicitStartupScene;
        std::string warning;
        if (TryReadRuntimeStartupScene(options.settingsPath, explicitStartupScene, &warning))
        {
            selection.source = RuntimeStartupSceneSource::RuntimeSettings;
            selection.configuredValue = explicitStartupScene;
            selection.warning = PrefixStartupWarning("settings", warning);
            if (!explicitStartupScene.empty())
                selection.scenePath = ResolveRuntimeStartupPath(explicitStartupScene);
            return selection;
        }
        selection.warning = PrefixStartupWarning("settings", warning);

        if (options.allowEditorRestore)
        {
            std::string editorWarning;
            const std::string lastScene = ReadEditorStartupScene(options.editorConfigPath, &editorWarning);
            if (!editorWarning.empty())
            {
                const std::string prefixedWarning = PrefixStartupWarning("editor_config", editorWarning);
                selection.warning =
                    selection.warning.empty() ? prefixedWarning : selection.warning + "; " + prefixedWarning;
            }
            if (!lastScene.empty())
            {
                selection.source = RuntimeStartupSceneSource::EditorConfig;
                selection.configuredValue = lastScene;
                selection.scenePath = ResolveRuntimeStartupPath(lastScene);
                return selection;
            }
        }

        if (options.allowProjectFallback && projectSelection.loadedManifest && !projectSelection.project.startupScene.empty())
        {
            selection.source = RuntimeStartupSceneSource::ProjectManifest;
            selection.configuredValue = projectSelection.project.startupScene.generic_string();
            selection.scenePath = projectSelection.project.ResolveStartupScene();
        }

        return selection;
    }

    RuntimeStartupSceneSettings ReadRuntimeStartupSceneSettings(const RuntimeStartupSceneSelection &selection)
    {
        RuntimeStartupSceneSettings out;
        if (selection.scenePath.empty())
            return out;

        FileSystem sceneFile(selection.scenePath.string(), std::ios::in | std::ios::binary);
        if (!sceneFile.IsOpen())
            return out;

        const std::string sceneText = sceneFile.ReadAll();
        rapidjson::Document scene;
        scene.Parse(sceneText.c_str(), sceneText.size());
        if (scene.HasParseError() || !scene.IsObject())
            return out;

        auto settingsIt = scene.FindMember("settings");
        if (settingsIt == scene.MemberEnd() || !settingsIt->value.IsObject())
            return out;

        const rapidjson::Value &settings = settingsIt->value;
        auto presentIt = settings.FindMember("present_mode");
        if (presentIt != settings.MemberEnd() && presentIt->value.IsInt())
        {
            const int mode = presentIt->value.GetInt();
            if (mode >= 0 && mode < PE_PRESENT_MODE_COUNT)
                out.presentMode = static_cast<PePresentMode>(mode);
        }

        auto scaleIt = settings.FindMember("render_scale");
        if (scaleIt != settings.MemberEnd() && scaleIt->value.IsNumber())
            out.renderScale = std::clamp(scaleIt->value.GetFloat(), kMinRenderScale, kMaxRenderScale);

        return out;
    }

    const char *PresentModeToConfigToken(PePresentMode mode)
    {
        switch (mode)
        {
        case PE_PRESENT_MODE_IMMEDIATE:
            return "immediate";
        case PE_PRESENT_MODE_MAILBOX:
            return "mailbox";
        case PE_PRESENT_MODE_FIFO:
            return "fifo";
        case PE_PRESENT_MODE_FIFO_RELAXED:
            return "fifo_relaxed";
        default:
            return "fifo";
        }
    }

    std::optional<PePresentMode> ParsePresentModeToken(std::string_view token)
    {
        std::string normalized;
        normalized.reserve(token.size());
        for (char c : token)
        {
            const char lower = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
            normalized.push_back(lower == '-' ? '_' : lower);
        }

        if (normalized == "immediate")
            return PE_PRESENT_MODE_IMMEDIATE;
        if (normalized == "mailbox")
            return PE_PRESENT_MODE_MAILBOX;
        if (normalized == "fifo")
            return PE_PRESENT_MODE_FIFO;
        if (normalized == "fifo_relaxed")
            return PE_PRESENT_MODE_FIFO_RELAXED;
        return std::nullopt;
    }

    std::optional<PePresentMode> ReadPresentModeEnvOverride()
    {
        std::string value;
#if defined(PE_WIN32)
        char *raw = nullptr;
        size_t rawSize = 0;
        if (::_dupenv_s(&raw, &rawSize, kPresentModeEnvVar) != 0 || !raw)
            return std::nullopt;
        value = raw;
        std::free(raw);
#else
        const char *raw = std::getenv(kPresentModeEnvVar);
        if (!raw)
            return std::nullopt;
        value = raw;
#endif
        if (value.empty())
            return std::nullopt;
        return ParsePresentModeToken(value);
    }

    std::optional<PePresentMode> ReadEditorPresentMode(const std::filesystem::path &editorConfigPath,
                                                       std::string *warning)
    {
        rapidjson::Document document;
        std::string loadWarning;
        project_detail::TryLoadJsonObject(RuntimeEditorConfigPath(editorConfigPath), document, loadWarning);
        if (warning)
            *warning = loadWarning;

        const std::string token = project_detail::ReadJsonStringField(document, kEditorPresentModeKey);
        if (token.empty())
            return std::nullopt;
        return ParsePresentModeToken(token);
    }

    bool WriteEditorPresentMode(const std::filesystem::path &editorConfigPath,
                                PePresentMode mode,
                                std::string *error)
    {
        const std::filesystem::path path = RuntimeEditorConfigWritePath(editorConfigPath);
        rapidjson::Document document;
        std::string warning;
        project_detail::TryLoadJsonObject(path, document, warning);

        project_detail::SetJsonStringMember(document, kEditorPresentModeKey, PresentModeToConfigToken(mode));
        return project_detail::WriteJsonObject(path, document, error);
    }

    bool ClearEditorPresentMode(const std::filesystem::path &editorConfigPath, std::string *error)
    {
        const std::filesystem::path path = RuntimeEditorConfigWritePath(editorConfigPath);
        rapidjson::Document document;
        std::string warning;
        project_detail::TryLoadJsonObject(path, document, warning);

        if (document.HasMember(kEditorPresentModeKey))
            document.RemoveMember(kEditorPresentModeKey);
        return project_detail::WriteJsonObject(path, document, error);
    }

    std::optional<PePresentMode> ReadStartupPresentModeOverride(const std::filesystem::path &editorConfigPath)
    {
        if (const std::optional<PePresentMode> envMode = ReadPresentModeEnvOverride())
            return envMode;
        return ReadEditorPresentMode(editorConfigPath);
    }
} // namespace pe
