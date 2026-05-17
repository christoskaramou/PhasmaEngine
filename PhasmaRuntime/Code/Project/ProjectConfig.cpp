#include "Project/ProjectConfig.h"

#include "Project/Detail/ProjectHelpers.h"

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/prettywriter.h"

#include <fstream>

namespace pe
{
    namespace
    {
        bool TryReadString(const rapidjson::Value &object,
                           const char *key,
                           std::string &out,
                           std::string *error,
                           bool required)
        {
            auto it = object.FindMember(key);
            if (it == object.MemberEnd())
            {
                if (required)
                {
                    project_detail::SetError(error, std::string("Project manifest field '") + key + "' is required");
                    return false;
                }
                return true;
            }

            if (!it->value.IsString())
            {
                project_detail::SetError(error, std::string("Project manifest field '") + key + "' must be a string");
                return false;
            }

            out = it->value.GetString();
            return true;
        }

        bool TryReadPath(const rapidjson::Value &object,
                         const char *key,
                         std::filesystem::path &out,
                         std::string *error,
                         bool required)
        {
            std::string value;
            if (!TryReadString(object, key, value, error, required))
                return false;

            if (object.HasMember(key))
                out = value;

            return true;
        }

        bool TryReadVersion(const rapidjson::Value &object, std::string *error)
        {
            auto it = object.FindMember("version");
            if (it == object.MemberEnd())
            {
                project_detail::SetError(error, "Project manifest field 'version' is required");
                return false;
            }

            if (!it->value.IsUint())
            {
                project_detail::SetError(error, "Project manifest field 'version' must be an unsigned integer");
                return false;
            }

            if (it->value.GetUint() != 1)
            {
                project_detail::SetError(error, "Unsupported project manifest version " + std::to_string(it->value.GetUint()));
                return false;
            }

            return true;
        }

        void AddStringMember(rapidjson::Document &document, const char *key, const std::string &value)
        {
            rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
            rapidjson::Value jsonKey(key, allocator);
            rapidjson::Value jsonValue;
            jsonValue.SetString(value.c_str(), static_cast<rapidjson::SizeType>(value.size()), allocator);
            document.AddMember(jsonKey, jsonValue, allocator);
        }
    } // namespace

    ProjectConfig ProjectConfig::ForRoot(const std::filesystem::path &projectRoot)
    {
        ProjectConfig config;
        config.root = project_detail::Normalize(projectRoot);
        config.manifestPath = DefaultManifestPath(config.root);
        return config;
    }

    std::filesystem::path ProjectConfig::DefaultManifestPath(const std::filesystem::path &projectRoot)
    {
        return project_detail::NormalizeAgainst(projectRoot, kProjectManifestFileName);
    }

    std::optional<ProjectConfig> ProjectConfig::TryLoadManifest(const std::filesystem::path &path, std::string *error)
    {
        const std::filesystem::path manifestPath = project_detail::NormalizeAbsolute(path);
        std::ifstream file(manifestPath, std::ios::binary);
        if (!file)
        {
            project_detail::SetError(error, "Unable to open project manifest: " + manifestPath.generic_string());
            return std::nullopt;
        }

        project_detail::SkipUtf8Bom(file);
        rapidjson::IStreamWrapper stream(file);
        rapidjson::Document document;
        document.ParseStream(stream);
        if (document.HasParseError())
        {
            project_detail::SetError(error,
                                     "Invalid project manifest JSON at offset " + std::to_string(document.GetErrorOffset()) + ": " +
                                         rapidjson::GetParseError_En(document.GetParseError()));
            return std::nullopt;
        }

        if (!document.IsObject())
        {
            project_detail::SetError(error, "Project manifest root must be a JSON object");
            return std::nullopt;
        }

        if (!TryReadVersion(document, error))
            return std::nullopt;

        ProjectConfig config = ForRoot(manifestPath.parent_path());
        config.manifestPath = manifestPath;

        if (!TryReadString(document, "name", config.name, error, true))
            return std::nullopt;
        if (!TryReadPath(document, "assets", config.assetsDirectory, error, true))
            return std::nullopt;
        if (!TryReadPath(document, "startup_scene", config.startupScene, error, false))
            return std::nullopt;

        if (config.name.empty())
        {
            project_detail::SetError(error, "Project manifest field 'name' must not be empty");
            return std::nullopt;
        }
        if (config.assetsDirectory.empty())
        {
            project_detail::SetError(error, "Project manifest field 'assets' must not be empty");
            return std::nullopt;
        }

        return config;
    }

    std::filesystem::path ProjectConfig::AssetsRoot() const
    {
        return project_detail::NormalizeAgainst(root, assetsDirectory);
    }

    std::filesystem::path ProjectConfig::ResolveProjectPath(const std::filesystem::path &path) const
    {
        return project_detail::NormalizeAgainst(root, path);
    }

    std::filesystem::path ProjectConfig::ResolveAssetPath(const std::filesystem::path &path) const
    {
        return project_detail::NormalizeAgainst(AssetsRoot(), path);
    }

    std::filesystem::path ProjectConfig::ResolveStartupScene() const
    {
        return ResolveProjectPath(startupScene);
    }

    bool ProjectConfig::WriteManifest(const std::filesystem::path &path, std::string *error) const
    {
        const std::filesystem::path manifestPath = project_detail::NormalizeAbsolute(path);
        const std::filesystem::path parent = manifestPath.parent_path();
        if (!parent.empty())
        {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec)
            {
                project_detail::SetError(error, "Unable to create project manifest directory: " + parent.generic_string());
                return false;
            }
        }

        std::ofstream file(manifestPath);
        if (!file)
        {
            project_detail::SetError(error, "Unable to write project manifest: " + manifestPath.generic_string());
            return false;
        }

        rapidjson::Document document(rapidjson::kObjectType);
        rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
        document.AddMember("version", 1u, allocator);
        AddStringMember(document, "name", name);
        AddStringMember(document, "assets", assetsDirectory.generic_string());
        AddStringMember(document, "startup_scene", startupScene.generic_string());

        rapidjson::OStreamWrapper stream(file);
        rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(stream);
        writer.SetIndent(' ', 4);
        if (!document.Accept(writer))
        {
            project_detail::SetError(error, "Unable to serialize project manifest");
            return false;
        }

        return true;
    }
} // namespace pe
