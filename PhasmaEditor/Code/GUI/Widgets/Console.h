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
        const LogEntry *GetLatestLog() const { return m_logs.empty() ? nullptr : &m_logs.back(); }
        const std::vector<LogEntry> &GetLogs() const { return m_logs; }
        int GetWarnCount() const { return m_warnCount; }
        int GetErrorCount() const { return m_errorCount; }
        void FocusWithFilter(const char *filter)
        {
            m_open = true;
            m_scrollToBottom = true;
            strncpy_s(m_filter.InputBuf, IM_ARRAYSIZE(m_filter.InputBuf), filter, _TRUNCATE);
            m_filter.Build();
            ImGui::SetWindowFocus(m_name.c_str());
        }

    private:
        std::vector<LogEntry> m_logs;
        ImGuiTextFilter m_filter;
        bool m_autoScroll;
        bool m_scrollToBottom;
        int m_warnCount = 0;
        int m_errorCount = 0;
    };
} // namespace pe
