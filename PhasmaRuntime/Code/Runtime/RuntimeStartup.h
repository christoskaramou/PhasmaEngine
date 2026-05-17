#pragma once

#include "API/RHITypes.h"
#include "Project/ProjectSelection.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace pe
{
    inline constexpr const char *kEditorConfigRelativePath = "Assets/editor_config.json";
    inline constexpr const char *kEditorLastSceneKey = "last_scene";

    enum class RuntimeStartupSceneSource : uint8_t
    {
        None,
        RuntimeSettings,
        EditorConfig,
        ProjectManifest
    };

    struct RuntimeStartupSceneSelection
    {
        RuntimeStartupSceneSource source = RuntimeStartupSceneSource::None;
        std::filesystem::path scenePath;
        std::string configuredValue;
        std::string warning;

        [[nodiscard]] bool HasSelection() const { return source != RuntimeStartupSceneSource::None; }
        [[nodiscard]] bool IsExplicitEmpty() const
        {
            return source == RuntimeStartupSceneSource::RuntimeSettings && configuredValue.empty();
        }
    };

    struct RuntimeStartupSceneSettings
    {
        std::optional<PePresentMode> presentMode;
        std::optional<float> renderScale;
    };

    struct RuntimeStartupSceneResolveOptions
    {
        bool allowEditorRestore = true;
        bool allowProjectFallback = true;
        std::filesystem::path settingsPath;
        std::filesystem::path editorConfigPath;
    };

    [[nodiscard]] const char *RuntimeStartupSceneSourceName(RuntimeStartupSceneSource source);
    [[nodiscard]] std::filesystem::path RuntimeEditorConfigPath(const std::filesystem::path &editorConfigPath = {});
    [[nodiscard]] std::filesystem::path RuntimeEditorConfigWritePath(const std::filesystem::path &editorConfigPath = {});
    [[nodiscard]] std::string ReadEditorStartupScene(const std::filesystem::path &editorConfigPath = {},
                                                     std::string *warning = nullptr);
    [[nodiscard]] bool WriteEditorStartupScene(const std::filesystem::path &editorConfigPath,
                                               const std::string &startupScene,
                                               std::string *error = nullptr);
    [[nodiscard]] RuntimeStartupSceneSelection ResolveRuntimeStartupScene(
        const ProjectSelection &projectSelection,
        const RuntimeStartupSceneResolveOptions &options = {});
    [[nodiscard]] RuntimeStartupSceneSettings ReadRuntimeStartupSceneSettings(
        const RuntimeStartupSceneSelection &selection);
} // namespace pe
