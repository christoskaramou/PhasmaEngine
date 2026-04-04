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
        std::string CreateNode(const std::string &name, const std::string &parent) const;
        std::string SetNodeTransform(const std::string &nodeId, const float *pos, const float *rot, const float *scale) const;
        std::string AddMeshToNode(const std::string &nodeId, const std::string &primitive) const;
        std::string SetNodeMaterial(const std::string &nodeId, int slot, const std::string &propsJson) const;
        std::string AddLight(const std::string &type) const;
        std::string QueryScene() const;
        std::string FindLoadableModel(const std::string &query) const;
        std::string ReadAgentFile(const std::string &path) const;
        std::string WriteAgentFile(const std::string &path, const std::string &content, bool append) const;
        std::string TakeScreenshot() const;
        std::string TakeProfilerSnapshot() const;
        std::string QueryImGuiWindows() const;
        std::string InjectMouseInput(const std::string &args) const;
        std::string ReloadModule() const;

    private:
        void QueueAction(std::function<void()> fn) const;

        QueueActionFn m_queueAction;
        void *m_sdlWindow = nullptr; // SDL_Window*, stored as void* to avoid SDL header dependency
    };
} // namespace pe
