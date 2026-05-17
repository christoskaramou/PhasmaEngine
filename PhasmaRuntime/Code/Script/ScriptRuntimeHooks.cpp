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

    void SetScriptModelLoading(bool loading)
    {
        if (s_scriptRuntimeHooks.setModelLoading)
            s_scriptRuntimeHooks.setModelLoading(loading);
    }
} // namespace pe
