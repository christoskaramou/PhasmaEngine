#include "Project/ProjectSelection.h"

#include "Base/Path.h"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/prettywriter.h"

#include <fstream>
#include <system_error>
#include <utility>

namespace pe
{
    namespace
    {
        void SetError(std::string *error, std::string message)
        {
            if (error)
                *error = std::move(message);
        }

        std::filesystem::path Normalize(const std::filesystem::path &path)
        {
            return path.lexically_normal();
        }

        std::filesystem::path NormalizeAbsolute(const std::filesystem::path &path)
        {
            if (path.empty())
                return {};

            std::error_code ec;
            const std::filesystem::path absolutePath = path.is_absolute() ? path : std::filesystem::absolute(path, ec);
            return Normalize(ec ? path : absolutePath);
        }

        std::filesystem::path ResolveSettingsPathValue(const std::filesystem::path &settingsPath,
                                                       const std::string &value)
        {
            std::filesystem::path path(value);
            if (path.is_absolute())
                return Normalize(path);

            return Normalize(settingsPath.parent_path() / path);
        }

        std::filesystem::path DefaultProjectRoot()
        {
            Path::Init();
            if (!Path::Assets.empty())
                return NormalizeAbsolute(Path::Assets);
            return NormalizeAbsolute(Path::Executable);
        }

        bool TryLoadJsonObject(const std::filesystem::path &path, rapidjson::Document &document, std::string &warning)
        {
            document.SetObject();

            std::error_code ec;
            if (!std::filesystem::exists(path, ec))
                return true;

            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                warning = "Could not open " + path.generic_string();
                return false;
            }

            rapidjson::IStreamWrapper stream(file);
            document.ParseStream(stream);
            if (document.HasParseError())
            {
                warning = "Could not parse " + path.generic_string() + " at offset " +
                          std::to_string(document.GetErrorOffset()) + ": " +
                          rapidjson::GetParseError_En(document.GetParseError());
                document.SetObject();
                return false;
            }

            if (!document.IsObject())
            {
                warning = path.generic_string() + " must contain a JSON object";
                document.SetObject();
                return false;
            }

            return true;
        }

        std::string ReadJsonStringField(const rapidjson::Document &document, const char *key)
        {
            if (!document.HasMember(key) || !document[key].IsString())
                return {};

            return document[key].GetString();
        }

        void SetJsonStringMember(rapidjson::Document &document, const char *key, const std::string &value)
        {
            rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
            rapidjson::Value jsonKey(key, allocator);
            rapidjson::Value jsonValue;
            jsonValue.SetString(value.c_str(), static_cast<rapidjson::SizeType>(value.size()), allocator);

            if (document.HasMember(key))
                document[key] = jsonValue;
            else
                document.AddMember(jsonKey.Move(), jsonValue.Move(), allocator);
        }

        bool WriteJsonObject(const std::filesystem::path &path, const rapidjson::Document &document, std::string *error)
        {
            const std::filesystem::path parent = path.parent_path();
            if (!parent.empty())
            {
                std::error_code ec;
                std::filesystem::create_directories(parent, ec);
                if (ec)
                {
                    SetError(error, "Could not create " + parent.generic_string() + ": " + ec.message());
                    return false;
                }
            }

            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file)
            {
                SetError(error, "Could not open " + path.generic_string() + " for writing");
                return false;
            }

            rapidjson::OStreamWrapper stream(file);
            rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(stream);
            writer.SetIndent(' ', 2);
            if (!document.Accept(writer))
            {
                SetError(error, "Could not serialize " + path.generic_string());
                return false;
            }

            file << '\n';
            return true;
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
    } // namespace

    std::filesystem::path DefaultProjectSettingsPath()
    {
        Path::Init();
        return NormalizeAbsolute(std::filesystem::path(Path::Executable) / kRuntimeSettingsFileName);
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
        selection.settingsPath = settingsPath.empty() ? DefaultProjectSettingsPath() : NormalizeAbsolute(settingsPath);

        rapidjson::Document document;
        std::string warning;
        TryLoadJsonObject(selection.settingsPath, document, warning);
        selection.warning = warning;

        const std::string manifest = ReadJsonStringField(document, kProjectManifestSettingsKey);
        if (!manifest.empty() &&
            TryUseManifest(selection,
                           ResolveSettingsPathValue(selection.settingsPath, manifest),
                           ProjectSelectionSource::RuntimeSettingsManifest))
        {
            return selection;
        }

        const std::string projectPath = ReadJsonStringField(document, kProjectPathSettingsKey);
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
            settingsPath.empty() ? DefaultProjectSettingsPath() : NormalizeAbsolute(settingsPath);

        startupScene.clear();
        rapidjson::Document document;
        std::string warning;
        TryLoadJsonObject(normalizedSettingsPath, document, warning);
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
            settingsPath.empty() ? DefaultProjectSettingsPath() : NormalizeAbsolute(settingsPath);

        rapidjson::Document document;
        std::string warning;
        TryLoadJsonObject(normalizedSettingsPath, document, warning);

        SetJsonStringMember(document, kProjectPathSettingsKey, project.root.generic_string());
        if (!project.manifestPath.empty())
            SetJsonStringMember(document, kProjectManifestSettingsKey, project.manifestPath.generic_string());
        else if (document.HasMember(kProjectManifestSettingsKey))
            document.RemoveMember(kProjectManifestSettingsKey);

        return WriteJsonObject(normalizedSettingsPath, document, error);
    }

    bool WriteRuntimeStartupScene(const std::filesystem::path &settingsPath,
                                  const std::string &startupScene,
                                  std::string *error)
    {
        const std::filesystem::path normalizedSettingsPath =
            settingsPath.empty() ? DefaultProjectSettingsPath() : NormalizeAbsolute(settingsPath);

        rapidjson::Document document;
        std::string warning;
        TryLoadJsonObject(normalizedSettingsPath, document, warning);

        SetJsonStringMember(document, kStartupSceneSettingsKey, startupScene);
        return WriteJsonObject(normalizedSettingsPath, document, error);
    }
} // namespace pe
