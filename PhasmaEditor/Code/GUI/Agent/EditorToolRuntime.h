#pragma once

namespace pe
{
    class EditorToolRuntime
    {
    public:
        using QueueActionFn = std::function<void(std::function<void()>)>;

        EditorToolRuntime() = default;
        EditorToolRuntime(QueueActionFn queueAction, void *sdlWindow = nullptr);

        std::string ExecuteLua(const std::string &code) const;
        std::string FindLoadableModel(const std::string &query) const;
        std::string ReadAgentFile(const std::string &path) const;
        std::string WriteAgentFile(const std::string &path, const std::string &content, bool append) const;
        std::string TakeScreenshot() const;
        std::string TakeProfilerSnapshot() const;
        std::string QueryImGuiWindows() const;
        std::string InjectMouseInput(const std::string &args) const;

    private:
        void QueueAction(std::function<void()> fn) const;

        QueueActionFn m_queueAction;
        void *m_sdlWindow = nullptr; // SDL_Window*, stored as void* to avoid SDL header dependency
    };
} // namespace pe
