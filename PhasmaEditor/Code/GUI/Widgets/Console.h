#pragma once
#include "GUI/Widget.h"
#include "imgui/imgui.h"
#include "Base/Log.h"

namespace pe
{
    struct LogEntry
    {
        std::string text;
        LogType type;
    };

    class Console : public Widget
    {
    public:
        Console() : Widget("Console") 
        {
            m_autoScroll = true;
            m_scrollToBottom = false;
            Clear();
        }

        void Update() override;
        void Clear();
        void AddLog(LogType type, const char *fmt, ...) IM_FMTARGS(3);

    private:
        std::vector<LogEntry> m_logs;
        ImGuiTextFilter m_filter;
        bool m_autoScroll;
        bool m_scrollToBottom;
    };
} // namespace pe
