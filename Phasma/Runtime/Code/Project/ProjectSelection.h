#pragma once

#include "Project/ProjectConfig.h"

namespace pe
{
    inline constexpr const char *kRuntimeSettingsFileName = "phasma_settings.json";
    inline constexpr const char *kProjectManifestSettingsKey = "project_manifest";
    inline constexpr const char *kProjectPathSettingsKey = "project_path";
    inline constexpr const char *kStartupSceneSettingsKey = "startup_scene";
    inline constexpr const char *kAudioMutedSettingsKey = "audio_muted";

    enum class ProjectSelectionSource : uint8_t
    {
        RuntimeSettingsManifest,
        RuntimeSettingsProjectPathManifest,
        RuntimeSettingsProjectPath,
        BuiltInAssetsRoot
    };

    struct ProjectSelection
    {
        ProjectConfig project;
        ProjectSelectionSource source = ProjectSelectionSource::BuiltInAssetsRoot;
        std::filesystem::path settingsPath;
        std::string warning;
        bool loadedManifest = false;
    };

    [[nodiscard]] std::filesystem::path DefaultProjectSettingsPath();
    [[nodiscard]] const char *ProjectSelectionSourceName(ProjectSelectionSource source);
    [[nodiscard]] ProjectSelection ResolveProjectSelection(const std::filesystem::path &settingsPath = {});
    [[nodiscard]] std::filesystem::path ProjectSelectionAssetsRoot(const ProjectSelection &selection);
    void ApplyProjectSelectionAssetsRoot(const ProjectSelection &selection);
    [[nodiscard]] std::string ReadRuntimeStartupScene(const std::filesystem::path &settingsPath = {},
                                                      std::string *warning = nullptr);
    [[nodiscard]] bool TryReadRuntimeStartupScene(const std::filesystem::path &settingsPath,
                                                  std::string &startupScene,
                                                  std::string *warning = nullptr);
    // "audio_muted": true starts every run from these settings silent (test and review launches).
    [[nodiscard]] bool ReadRuntimeAudioMuted(const std::filesystem::path &settingsPath = {});
    [[nodiscard]] bool WriteProjectSelection(const std::filesystem::path &settingsPath,
                                             const ProjectConfig &project,
                                             std::string *error = nullptr);
    [[nodiscard]] bool WriteRuntimeStartupScene(const std::filesystem::path &settingsPath,
                                                const std::string &startupScene,
                                                std::string *error = nullptr);
} // namespace pe
