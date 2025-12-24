#pragma once
#include "Widget.h"

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

        const std::vector<GpuTimerSample> &GetGpuTimerInfos() { return m_gpuTimerInfos; }
        std::vector<GpuTimerSample> &GetGpuTimerInfosMutable() { return m_gpuTimerInfos; }
        uint32_t GetDockspaceId() const { return m_dockspaceId; }

    private:
        static void async_fileDialog_ImGuiMenuItem(const char *menuLabel, const char *dialogTitle, const std::vector<const char *> &filter);
        static void async_messageBox_ImGuiMenuItem(const char *menuLabel, const char *messageBoxTitle, const char *message);

        void Menu();
        void BuildDockspace();
        void ResetDockspaceLayout(uint32_t dockspaceId);

        bool m_render;
        std::unique_ptr<Attachment> m_attachment;
        bool m_show_demo_window;
        uint32_t m_dockspaceId;
        bool m_dockspaceInitialized;
        bool m_requestDockReset;
        std::vector<GpuTimerSample> m_gpuTimerInfos;

        std::vector<std::shared_ptr<Widget>> m_widgets;
        std::vector<std::shared_ptr<Widget>> m_menuWindowWidgets;
        std::vector<std::shared_ptr<Widget>> m_menuAssetsWidgets;
    };
} // namespace pe
