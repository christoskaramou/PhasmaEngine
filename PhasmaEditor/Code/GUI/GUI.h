#pragma once

struct ImGuiStyle;

namespace pe
{
    constexpr float TITLEBAR_HEIGHT = 19.f;

    class CommandBuffer;
    class Queue;
    class Image;

    class GUI
    {
    public:
        GUI();
        ~GUI();

        void Init();
        void Update();
        void Draw(CommandBuffer *cmd);
        void DrawPlatformWindows();
        bool Render() { return m_render; }
        void ToggleRender() { m_render = !m_render; }

    private:
        static void async_fileDialog_ImGuiMenuItem(const char *menuLabel, const char *dialogTitle, const std::vector<const char *> &filter);
        static void async_messageBox_ImGuiMenuItem(const char *menuLabel, const char *messageBoxTitle, const char *message);

        void Menu();
        void BuildDockspace();
        void ResetDockspaceLayout(uint32_t dockspaceId);
        void Loading();
        void Metrics();
        void Shaders();
        void Models();
        void Scripts();
        void AssetViewer();
        void SceneView();
        void Properties();
        float FetchTotalGPUTime();
        void ShowGpuTimings(float maxTime);
        void ShowGpuTimingsTable(float totalMs);

        bool m_render;
        std::unique_ptr<Attachment> m_attachment;
        bool m_show_demo_window;
        uint32_t m_dockspaceId;
        bool m_dockspaceInitialized;
        bool m_requestDockReset;
        std::vector<GpuTimerInfo> m_gpuTimerInfos;
        void *m_viewportTextureId;
        bool m_sceneViewFloating;
        bool m_sceneViewRedockQueued;
        Image *m_sceneViewImage;
    };
}
