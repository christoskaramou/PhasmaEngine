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
        bool Render() const { return m_render; }
        void ToggleRender() { m_render = !m_render; }
        void TriggerExitConfirmation() { m_showExitConfirmation = true; }

        // Thread-safe GpuTimer access
        std::vector<GpuTimerSample> PopGpuTimerInfos();

        uint32_t GetDockspaceId() const { return m_dockspaceId; }

        template <typename T>
        T *GetWidget()
        {
            for (auto &widget : m_widgets)
            {
                if (auto *p = dynamic_cast<T *>(widget.get()))
                    return p;
            }
            return nullptr;
        }

    private:
        void ShowLoadModelMenuItem();
        void ShowExitMenuItem();
        void DrawExitPopup();

        bool m_showExitConfirmation = false;

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
        std::mutex m_timerMutex;

        std::vector<std::shared_ptr<Widget>> m_widgets;
        std::vector<std::shared_ptr<Widget>> m_menuWindowWidgets;
        std::vector<std::shared_ptr<Widget>> m_menuAssetsWidgets;
    };
} // namespace pe
