#pragma once

#include "Script/ScriptSystem.h"

namespace pe
{
    class EditorToolRuntime
    {
    public:
        using QueueActionFn = std::function<void(std::function<void()>)>;

        EditorToolRuntime() = default;
        EditorToolRuntime(ScriptSystem *scriptSystem, QueueActionFn queueAction, void *sdlWindow = nullptr);

        void SetScriptSystem(ScriptSystem *scriptSystem) { m_scriptSystem = scriptSystem; }
        void SetQueueAction(QueueActionFn queueAction) { m_queueAction = std::move(queueAction); }

        std::string ExecuteLua(const std::string &code) const;
        std::string FindLoadableModel(const std::string &query) const;
        std::string ReadAgentFile(const std::string &path) const;
        std::string WriteAgentFile(const std::string &path, const std::string &content, bool append) const;
        std::string TakeScreenshot() const;
        std::string QueryImGuiWindows() const;
        std::string InjectMouseInput(const std::string &args) const;

    private:
        void QueueAction(std::function<void()> fn) const;

        ScriptSystem *m_scriptSystem = nullptr;
        QueueActionFn m_queueAction;
        void *m_sdlWindow = nullptr; // SDL_Window*, stored as void* to avoid SDL header dependency
    };
} // namespace pe
