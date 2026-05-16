#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace pe
{
    inline constexpr const char *kProjectManifestFileName = "phasma_project.json";

    struct ProjectConfig
    {
        std::string name = "MyProject";
        std::filesystem::path root;
        std::filesystem::path manifestPath;
        std::filesystem::path assetsDirectory = "Assets";
        std::filesystem::path startupScene;

        [[nodiscard]] static ProjectConfig ForRoot(const std::filesystem::path &projectRoot);
        [[nodiscard]] static std::filesystem::path DefaultManifestPath(const std::filesystem::path &projectRoot);
        [[nodiscard]] static std::optional<ProjectConfig> TryLoadManifest(const std::filesystem::path &path,
                                                                          std::string *error = nullptr);

        [[nodiscard]] std::filesystem::path AssetsRoot() const;
        [[nodiscard]] std::filesystem::path ResolveProjectPath(const std::filesystem::path &path) const;
        [[nodiscard]] std::filesystem::path ResolveAssetPath(const std::filesystem::path &path) const;
        [[nodiscard]] std::filesystem::path ResolveStartupScene() const;
        [[nodiscard]] bool WriteManifest(const std::filesystem::path &path, std::string *error = nullptr) const;
    };
} // namespace pe
