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
        std::string CreateSpriteNode(const std::string &argsJson) const;
        std::string GetSpriteNode(const std::string &argsJson) const;
        std::string SetSpriteNode(const std::string &argsJson) const;
        std::string SetSpriteFrame(const std::string &argsJson) const;
        std::string ReloadSpriteMetadata(const std::string &argsJson) const;
        std::string PlaySpriteClip(const std::string &argsJson) const;
        std::string SetSpritePlayback(const std::string &argsJson) const;
        std::string PauseSprite(const std::string &argsJson) const;
        std::string StopSprite(const std::string &argsJson) const;
        std::string ReadSpriteMetadata(const std::string &argsJson) const;
        std::string ValidateSpriteAsset(const std::string &argsJson) const;
        std::string WriteSpriteMetadata(const std::string &argsJson) const;
        std::string AddSpriteFrame(const std::string &argsJson) const;
        std::string UpdateSpriteFrame(const std::string &argsJson) const;
        std::string RemoveSpriteFrame(const std::string &argsJson) const;
        std::string ReorderSpriteFrames(const std::string &argsJson) const;
        std::string AddSpriteClip(const std::string &argsJson) const;
        std::string UpdateSpriteClip(const std::string &argsJson) const;
        std::string RemoveSpriteClip(const std::string &argsJson) const;
        std::string ReadSpriteSheet(const std::string &argsJson) const;
        std::string WriteSpriteSheet(const std::string &argsJson) const;
        std::string CreateSpriteFromSheet(const std::string &argsJson) const;
        std::string SetNodeTransform(const std::string &nodeId, const float *pos, const float *rot, const float *scale) const;
        std::string AddMeshToNode(const std::string &nodeId, const std::string &primitive) const;
        std::string SetNodeMaterial(const std::string &nodeId, int slot, const std::string &propsJson) const;
        std::string AddLight(const std::string &type) const;
        std::string GetSceneInfo(bool includeTree) const;
        std::string GetNodeInfo(const std::string &argsJson) const;
        std::string QueryScene() const;
        std::string GetRendererStatus() const;
        std::string SetCamera(const std::string &argsJson) const;
        std::string FrameNode(const std::string &argsJson) const;
        std::string SetNodeTexture(const std::string &argsJson) const;
        std::string TakeSceneScreenshot(const std::string &argsJson) const;
        std::string ListImageResources(const std::string &argsJson) const;
        std::string CaptureImageResource(const std::string &argsJson) const;
        std::string ListBufferResources(const std::string &argsJson) const;
        std::string CaptureBufferResource(const std::string &argsJson) const;
        std::string ImportModel(const std::string &argsJson) const;
        std::string GetImportStatus(const std::string &argsJson) const;
        std::string LoadCookedMesh(const std::string &argsJson) const;
        std::string FindLoadableModel(const std::string &query) const;
        std::string ReadAgentFile(const std::string &path) const;
        std::string WriteAgentFile(const std::string &path, const std::string &content, bool append) const;
        std::string TakeScreenshot() const;
        std::string TakeProfilerSnapshot() const;
        std::string QueryImGuiWindows() const;
        std::string InjectMouseInput(const std::string &args) const;
        std::string QueryEditorActions() const;
        std::string SetEditorWindowOpen(const std::string &windowName, const std::string &args) const;
        std::string InvokeEditorAction(const std::string &actionId, const std::string &args) const;
        std::string ReloadModule() const;

    private:
        void QueueAction(std::function<void()> fn) const;

        QueueActionFn m_queueAction;
        void *m_sdlWindow = nullptr; // SDL_Window*, stored as void* to avoid SDL header dependency
    };
} // namespace pe
