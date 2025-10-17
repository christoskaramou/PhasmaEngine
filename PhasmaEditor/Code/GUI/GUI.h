#pragma once

struct ImGuiStyle;

namespace pe
{
    constexpr float TITLEBAR_HEIGHT = 19.f;

    class CommandBuffer;
    class Queue;

    class GUI
    {
    public:
        GUI();
        ~GUI();

        void Init();
        void Update();
        void Draw(CommandBuffer * cmd);
        bool Render() { return m_render; }
        void ToggleRender() { m_render = !m_render; }
    
    private:
        static void async_fileDialog_ImGuiMenuItem(const char *menuLabel, const char *dialogTitle, const std::vector<const char *> &filter);
        static void async_messageBox_ImGuiMenuItem(const char *menuLabel, const char *messageBoxTitle, const char *message);

        void Menu();
        void Loading();
        void Metrics();
        void Shaders();
        void Properties();
        float FetchTotalGPUTime();
        void ShowGpuTimings(float maxTime);
        void ShowGpuTimingsTable(float totalMs);

        bool m_render;
        std::unique_ptr<Attachment> m_attachment;
        bool m_show_demo_window;
        std::vector<GpuTimerInfo> m_gpuTimerInfos;
    };
}
