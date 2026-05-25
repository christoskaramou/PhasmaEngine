#include "Project/ProjectSelection.h"

#include "Project/Detail/ProjectHelpers.h"
#include "rapidjson/document.h"

#include <system_error>

namespace pe
{
    namespace
    {
        std::filesystem::path ResolveSettingsPathValue(const std::filesystem::path &settingsPath,
                                                       const std::string &value)
        {
            std::filesystem::path path(value);
            if (path.is_absolute())
                return project_detail::Normalize(path);

            return project_detail::Normalize(settingsPath.parent_path() / path);
        }

        std::filesystem::path DefaultProjectRoot()
        {
            Path::Init();
            if (!Path::Assets.empty())
                return project_detail::NormalizeAbsolute(Path::Assets);
            return project_detail::NormalizeAbsolute(Path::Executable);
        }

        bool TryUseManifest(ProjectSelection &selection,
                            const std::filesystem::path &manifestPath,
                            ProjectSelectionSource source)
        {
            std::string error;
            std::optional<ProjectConfig> project = ProjectConfig::TryLoadManifest(manifestPath, &error);
            if (!project)
            {
                if (!error.empty())
                    selection.warning = error;
                return false;
            }

            selection.project = std::move(*project);
            selection.source = source;
            selection.loadedManifest = true;
            return true;
        }

        std::string EnsureTrailingSlash(std::filesystem::path path)
        {
            std::string out = project_detail::NormalizeAbsolute(path).generic_string();
            if (!out.empty() && out.back() != '/')
                out += '/';
            return out;
        }
    } // namespace

    std::filesystem::path DefaultProjectSettingsPath()
    {
        Path::Init();
        return project_detail::NormalizeAbsolute(std::filesystem::path(Path::Root) / kRuntimeSettingsFileName);
    }

    const char *ProjectSelectionSourceName(ProjectSelectionSource source)
    {
        switch (source)
        {
        case ProjectSelectionSource::RuntimeSettingsManifest:
            return "runtime settings manifest";
        case ProjectSelectionSource::RuntimeSettingsProjectPathManifest:
            return "runtime settings project path manifest";
        case ProjectSelectionSource::RuntimeSettingsProjectPath:
            return "runtime settings project path";
        case ProjectSelectionSource::BuiltInAssetsRoot:
            return "built-in assets root";
        default:
            return "unknown";
        }
    }

    ProjectSelection ResolveProjectSelection(const std::filesystem::path &settingsPath)
    {
        ProjectSelection selection;
        selection.settingsPath = settingsPath.empty() ? DefaultProjectSettingsPath() : project_detail::NormalizeAbsolute(settingsPath);

        rapidjson::Document document;
        std::string warning;
        project_detail::TryLoadJsonObject(selection.settingsPath, document, warning);
        selection.warning = warning;

        const std::string manifest = project_detail::ReadJsonStringField(document, kProjectManifestSettingsKey);
        if (!manifest.empty() &&
            TryUseManifest(selection,
                           ResolveSettingsPathValue(selection.settingsPath, manifest),
                           ProjectSelectionSource::RuntimeSettingsManifest))
        {
            return selection;
        }

        const std::string projectPath = project_detail::ReadJsonStringField(document, kProjectPathSettingsKey);
        if (!projectPath.empty())
        {
            const std::filesystem::path projectRoot = ResolveSettingsPathValue(selection.settingsPath, projectPath);
            const std::filesystem::path defaultManifest = ProjectConfig::DefaultManifestPath(projectRoot);
            std::error_code ec;
            if (std::filesystem::exists(defaultManifest, ec) &&
                TryUseManifest(selection, defaultManifest, ProjectSelectionSource::RuntimeSettingsProjectPathManifest))
            {
                return selection;
            }

            selection.project = ProjectConfig::ForRoot(projectRoot);
            selection.source = ProjectSelectionSource::RuntimeSettingsProjectPath;
            selection.loadedManifest = false;
            return selection;
        }

        selection.project = ProjectConfig::ForRoot(DefaultProjectRoot());
        selection.source = ProjectSelectionSource::BuiltInAssetsRoot;
        selection.loadedManifest = false;
        return selection;
    }

    std::filesystem::path ProjectSelectionAssetsRoot(const ProjectSelection &selection)
    {
        if (selection.loadedManifest)
            return selection.project.AssetsRoot();

        return selection.project.root;
    }

    void ApplyProjectSelectionAssetsRoot(const ProjectSelection &selection)
    {
        Path::Init();
        const std::filesystem::path assetsRoot = ProjectSelectionAssetsRoot(selection);
        if (!assetsRoot.empty())
            Path::Assets = EnsureTrailingSlash(assetsRoot);
    }

    std::string ReadRuntimeStartupScene(const std::filesystem::path &settingsPath, std::string *warningOut)
    {
        std::string startupScene;
        const bool found = TryReadRuntimeStartupScene(settingsPath, startupScene, warningOut);
        (void)found;
        return startupScene;
    }

    bool TryReadRuntimeStartupScene(const std::filesystem::path &settingsPath,
                                    std::string &startupScene,
                                    std::string *warningOut)
    {
        const std::filesystem::path normalizedSettingsPath =
            settingsPath.empty() ? DefaultProjectSettingsPath() : project_detail::NormalizeAbsolute(settingsPath);

        startupScene.clear();
        rapidjson::Document document;
        std::string warning;
        project_detail::TryLoadJsonObject(normalizedSettingsPath, document, warning);
        if (warningOut)
            *warningOut = warning;

        if (!document.HasMember(kStartupSceneSettingsKey) || !document[kStartupSceneSettingsKey].IsString())
            return false;

        startupScene = document[kStartupSceneSettingsKey].GetString();
        return true;
    }

    bool WriteProjectSelection(const std::filesystem::path &settingsPath, const ProjectConfig &project, std::string *error)
    {
        const std::filesystem::path normalizedSettingsPath =
            settingsPath.empty() ? DefaultProjectSettingsPath() : project_detail::NormalizeAbsolute(settingsPath);

        rapidjson::Document document;
        std::string warning;
        project_detail::TryLoadJsonObject(normalizedSettingsPath, document, warning);

        project_detail::SetJsonStringMember(document, kProjectPathSettingsKey, project.root.generic_string());
        if (!project.manifestPath.empty())
            project_detail::SetJsonStringMember(document, kProjectManifestSettingsKey, project.manifestPath.generic_string());
        else if (document.HasMember(kProjectManifestSettingsKey))
            document.RemoveMember(kProjectManifestSettingsKey);

        return project_detail::WriteJsonObject(normalizedSettingsPath, document, error);
    }

    bool WriteRuntimeStartupScene(const std::filesystem::path &settingsPath,
                                  const std::string &startupScene,
                                  std::string *error)
    {
        const std::filesystem::path normalizedSettingsPath =
            settingsPath.empty() ? DefaultProjectSettingsPath() : project_detail::NormalizeAbsolute(settingsPath);

        rapidjson::Document document;
        std::string warning;
        project_detail::TryLoadJsonObject(normalizedSettingsPath, document, warning);

        project_detail::SetJsonStringMember(document, kStartupSceneSettingsKey, startupScene);
        return project_detail::WriteJsonObject(normalizedSettingsPath, document, error);
    }
} // namespace pe
