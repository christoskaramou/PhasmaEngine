#pragma once
#include "GUI/Widget.h"
#include "imgui/imgui.h"

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

        std::vector<LogEntry> GetLogs() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_logs;
        }

        int GetWarnCount() const { return m_warnCount; }
        int GetErrorCount() const { return m_errorCount; }
        void FocusWithFilter(const char *filter)
        {
            m_open = true;
            m_scrollToBottom = true;
            std::snprintf(m_filter.InputBuf, IM_ARRAYSIZE(m_filter.InputBuf), "%s", filter ? filter : "");
            m_filter.Build();
            ImGui::SetWindowFocus(m_name.c_str());
        }

    private:
        void TrimLogs();
        void MeasureLogs(float wrapWidth);

        std::vector<LogEntry> m_logs;
        std::vector<float> m_offsets; // m_offsets[i] = top of m_logs[i] in the wrapped layout, back() = total height
        float m_layoutWidth = 0.0f;
        float m_layoutFontSize = 0.0f;
        ImFont *m_layoutFont = nullptr;
        float m_scrollY = 0.0f;
        float m_trimmedHeight = 0.0f;
        mutable std::mutex m_mutex; // AddLog runs on whichever thread logs
        ImGuiTextFilter m_filter;
        bool m_autoScroll;
        bool m_scrollToBottom;
        int m_warnCount = 0;
        int m_errorCount = 0;
    };
} // namespace pe
