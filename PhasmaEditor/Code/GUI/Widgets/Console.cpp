#include "Console.h"

namespace pe
{
    void Console::Clear()
    {
        m_logs.clear();
    }

    void Console::AddLog(LogType type, const char *fmt, ...)
    {
        // Format string
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, IM_ARRAYSIZE(buf), fmt, args);
        buf[IM_ARRAYSIZE(buf) - 1] = 0;
        va_end(args);

        m_logs.push_back({ std::string(buf), type });

        if (m_autoScroll)
            m_scrollToBottom = true;
    }

    void Console::Update()
    {
        if (!ImGui::Begin(m_name.c_str(), &m_open))
        {
            ImGui::End();
            return;
        }

        // Options menu
        if (ImGui::BeginPopup("Options"))
        {
            ImGui::Checkbox("Auto-scroll", &m_autoScroll);
            ImGui::EndPopup();
        }

        // Main window
        if (ImGui::Button("Options"))
            ImGui::OpenPopup("Options");
        ImGui::SameLine();
        bool clear = ImGui::Button("Clear");
        ImGui::SameLine();
        bool copy = ImGui::Button("Copy");
        ImGui::SameLine();
        if (ImGui::Button("Open Log"))
        {
#if defined(_WIN32)
            system("start PhasmaEngine.log");
#elif defined(__linux__)
            system("xdg-open PhasmaEngine.log");
#endif
        }
        ImGui::SameLine();
        m_filter.Draw("Filter", -100.0f);

        if (clear)
            Clear();
        if (copy)
            ImGui::LogToClipboard();

        ImGui::Separator();
        ImGui::BeginChild("scrolling", ImVec2(0, 0), false, 0);

        if (copy)
            ImGui::LogToClipboard();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        // Draw Logs
        auto draw_log = [&](const LogEntry& log) {
            ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // Default White

            switch (log.type)
            {
            case LogType::Warn: color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); break; // Yellow
            case LogType::Error: color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); break; // Red
            default: break;
            }

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(log.text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        };

        if (m_filter.IsActive())
        {
            for (int i = 0; i < m_logs.size(); i++)
            {
                if (m_filter.PassFilter(m_logs[i].text.c_str()))
                    draw_log(m_logs[i]);
            }
        }
        else
        {
            ImGuiListClipper clipper;
            clipper.Begin((int)m_logs.size());
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                {
                    draw_log(m_logs[i]);
                }
            }
            clipper.End();
        }

        ImGui::PopStyleVar();

        if (m_scrollToBottom && (m_autoScroll || ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
            ImGui::SetScrollHereY(1.0f);
        m_scrollToBottom = false;

        ImGui::EndChild();
        ImGui::End();
    }
} // namespace pe
