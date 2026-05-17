#include "Runtime/RuntimeStartup.h"

#include "Base/Path.h"
#include "Project/Detail/ProjectHelpers.h"
#include "Project/ProjectPaths.h"

#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"

#include <algorithm>
#include <fstream>

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
                                                            bool allowProjectFallback,
                                                            const std::filesystem::path &settingsPath,
                                                            const std::filesystem::path &editorConfigPath)
    {
        RuntimeStartupSceneSelection selection;

        std::string explicitStartupScene;
        std::string warning;
        if (TryReadRuntimeStartupScene(settingsPath, explicitStartupScene, &warning))
        {
            selection.source = RuntimeStartupSceneSource::RuntimeSettings;
            selection.configuredValue = explicitStartupScene;
            selection.warning = PrefixStartupWarning("settings", warning);
            if (!explicitStartupScene.empty())
                selection.scenePath = ResolveRuntimeStartupPath(explicitStartupScene);
            return selection;
        }
        selection.warning = PrefixStartupWarning("settings", warning);

        std::string editorWarning;
        const std::string lastScene = ReadEditorStartupScene(editorConfigPath, &editorWarning);
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

        if (allowProjectFallback && projectSelection.loadedManifest && !projectSelection.project.startupScene.empty())
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

        std::ifstream sceneFile(selection.scenePath);
        if (!sceneFile)
            return out;

        rapidjson::IStreamWrapper stream(sceneFile);
        rapidjson::Document scene;
        scene.ParseStream(stream);
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
} // namespace pe
