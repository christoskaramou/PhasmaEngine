#include "Script/ScriptRuntimeHooks.h"

namespace pe
{
    namespace
    {
        ScriptRuntimeHooks s_scriptRuntimeHooks;
    } // namespace

    void SetScriptRuntimeHooks(ScriptRuntimeHooks hooks)
    {
        s_scriptRuntimeHooks = hooks;
    }

    bool IsScriptPlayMode()
    {
        return s_scriptRuntimeHooks.isPlayMode ? s_scriptRuntimeHooks.isPlayMode() : true;
    }

    void SetScriptPlayMode(bool enabled)
    {
        if (s_scriptRuntimeHooks.setPlayMode)
            s_scriptRuntimeHooks.setPlayMode(enabled);
    }

    bool IsScriptPaused()
    {
        return s_scriptRuntimeHooks.isPaused ? s_scriptRuntimeHooks.isPaused() : false;
    }

    void SetScriptPaused(bool paused)
    {
        if (s_scriptRuntimeHooks.setPaused)
            s_scriptRuntimeHooks.setPaused(paused);
    }

    bool IsScriptViewportFocused()
    {
        return s_scriptRuntimeHooks.isViewportFocused ? s_scriptRuntimeHooks.isViewportFocused() : true;
    }

    bool ShouldLoadEditorOnlyGlobalScripts()
    {
        return s_scriptRuntimeHooks.loadEditorOnlyGlobalScripts;
    }

    bool IsEditorHost()
    {
        return s_scriptRuntimeHooks.isEditorHost;
    }

    const std::string &GetEditorScriptAssetsRoot()
    {
        return s_scriptRuntimeHooks.editorAssetsRoot;
    }

    void SetScriptModelLoading(bool loading)
    {
        if (s_scriptRuntimeHooks.setModelLoading)
            s_scriptRuntimeHooks.setModelLoading(loading);
    }

    std::optional<std::string> ReadScriptLaunchOption(const std::string &name)
    {
        if (name.empty() || name.size() > 64 ||
            !std::all_of(name.begin(), name.end(), [](unsigned char c)
                         { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }))
            return std::nullopt;

        const std::string environmentName = name == "PE_PROJECT_VARIANT" ? name : "PE_SCRIPT_" + name;
#if defined(PE_WIN32)
        char *value = nullptr;
        size_t size = 0;
        if (::_dupenv_s(&value, &size, environmentName.c_str()) != 0 || !value)
            return std::nullopt;
        std::string result(value);
        std::free(value);
        return result;
#else
        const char *value = std::getenv(environmentName.c_str());
        return value ? std::optional<std::string>(value) : std::nullopt;
#endif
    }
} // namespace pe
