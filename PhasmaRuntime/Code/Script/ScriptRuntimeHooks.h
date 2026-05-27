#pragma once

#include <string>

namespace pe
{
    struct ScriptRuntimeHooks
    {
        bool (*isPlayMode)() = nullptr;
        void (*setPlayMode)(bool enabled) = nullptr;
        bool (*isPaused)() = nullptr;
        void (*setPaused)(bool paused) = nullptr;
        bool (*isViewportFocused)() = nullptr;
        void (*setModelLoading)(bool loading) = nullptr;
        bool loadEditorOnlyGlobalScripts = false;
        bool isEditorHost = false;
        std::string editorAssetsRoot;
    };

    void SetScriptRuntimeHooks(ScriptRuntimeHooks hooks);
    [[nodiscard]] bool IsScriptPlayMode();
    void SetScriptPlayMode(bool enabled);
    [[nodiscard]] bool IsScriptPaused();
    void SetScriptPaused(bool paused);
    [[nodiscard]] bool IsScriptViewportFocused();
    [[nodiscard]] bool ShouldLoadEditorOnlyGlobalScripts();
    [[nodiscard]] bool IsEditorHost();
    [[nodiscard]] const std::string &GetEditorScriptAssetsRoot();
    void SetScriptModelLoading(bool loading);
} // namespace pe
