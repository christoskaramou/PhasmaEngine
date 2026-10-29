#include "Console.h"

#include "GUI/GUIState.h"
#include "GUI/Helpers.h"

namespace pe
{
    namespace
    {
        constexpr size_t kMaxLogs = 10000;
        constexpr size_t kTrimLogs = kMaxLogs / 10; // trim in batches so the erase is paid once per kTrimLogs lines
    } // namespace

    void Console::Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_logs.clear();
        m_offsets.clear();
        m_trimmedHeight = 0.0f;
        m_warnCount = 0;
        m_errorCount = 0;
    }

    void Console::TrimLogs()
    {
        m_logs.erase(m_logs.begin(), m_logs.begin() + kTrimLogs);

        if (m_offsets.size() <= kTrimLogs)
        {
            m_offsets.clear();
            return;
        }

        const float removedHeight = m_offsets[kTrimLogs];
        m_offsets.erase(m_offsets.begin(), m_offsets.begin() + kTrimLogs);
        for (float &offset : m_offsets)
            offset -= removedHeight;

        m_trimmedHeight += removedHeight;
    }

    void Console::MeasureLogs(float wrapWidth)
    {
        ImFont *font = ImGui::GetFont();
        const float fontSize = ImGui::GetFontSize();
        const bool layoutChanged = wrapWidth != m_layoutWidth ||
                                   font != m_layoutFont ||
                                   fontSize != m_layoutFontSize;
        if (layoutChanged)
        {
            m_offsets.clear();
            m_layoutWidth = wrapWidth;
            m_layoutFont = font;
            m_layoutFontSize = fontSize;
        }

        if (m_offsets.empty())
            m_offsets.push_back(0.0f);

        // ItemSize truncates each line advance; truncate the same way so offsets match the drawn layout
        for (size_t i = m_offsets.size() - 1; i < m_logs.size(); i++)
        {
            const float height = ImGui::CalcTextSize(m_logs[i].text.c_str(), nullptr, false, wrapWidth).y;
            m_offsets.push_back(m_offsets.back() + ImTrunc(height));
        }
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

        std::lock_guard<std::mutex> lock(m_mutex);
        m_logs.push_back({std::string(buf), type});
        if (m_logs.size() > kMaxLogs)
            TrimLogs();

        if (type == LogType::Warn)
            ++m_warnCount;
        else if (type == LogType::Error)
            ++m_errorCount;

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
            ui::ItemTooltip("Keep the console scrolled to the newest log entry.");
            ImGui::EndPopup();
        }

        // Main window
        if (ImGui::Button("Options"))
            ImGui::OpenPopup("Options");
        ui::ItemTooltip("Open console display options.");
        ImGui::SameLine();
        bool clear = ImGui::Button("Clear");
        ui::ItemTooltip("Clear visible console entries and warning/error counters.");
        ImGui::SameLine();
        bool copy = ImGui::Button("Copy");
        ui::ItemTooltip("Copy visible console output to the clipboard.");
        ImGui::SameLine();
        if (ImGui::Button("Open Log"))
        {
            std::string logPath = Path::Root + "PhasmaEngine.log";
            GUIState::OpenExternalPath(logPath);
        }
        ui::ItemTooltip("Open the current engine log file externally.");
        ImGui::SameLine();
        m_filter.Draw("Filter", -100.0f);
        ui::ItemTooltip("Filter console messages by text.");

        if (clear)
            Clear();
        if (copy)
            ImGui::LogToClipboard();

        ImGui::Separator();

        std::lock_guard<std::mutex> lock(m_mutex);

        // Keep the same entries in view after TrimLogs removed lines above them
        if (m_trimmedHeight > 0.0f && !m_filter.IsActive())
            ImGui::SetNextWindowScroll(ImVec2(-1.0f, std::max(0.0f, m_scrollY - m_trimmedHeight)));
        m_trimmedHeight = 0.0f;

        ImGui::BeginChild("scrolling", ImVec2(0, 0), false, 0);

        if (copy)
            ImGui::LogToClipboard();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        // Draw Logs
        auto draw_log = [&](const LogEntry &log)
        {
            ImVec4 color;

            bool isLight = GUIState::s_guiStyle == GUIStyle::Light;
            switch (log.type)
            {
            case LogType::Info:
                color = isLight ? ImVec4(0.0f, 0.0f, 0.0f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                break;
            case LogType::Warn:
                color = isLight ? ImVec4(0.6f, 0.6f, 0.0f, 1.0f) : ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
                break;
            case LogType::Error:
                color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                break;
            default:
                color = isLight ? ImVec4(0.0f, 0.0f, 0.0f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                break;
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
            // ImGuiListClipper assumes equal heights; wrapped entries differ, so clip by measured offsets
            MeasureLogs(ImGui::CalcWrapWidthForPos(ImGui::GetCursorScreenPos(), 0.0f));

            const float viewTop = ImGui::GetScrollY() - ImGui::GetCursorPosY();
            const float viewBottom = viewTop + ImGui::GetWindowHeight();
            const auto firstIt = std::upper_bound(m_offsets.begin(), m_offsets.end(), viewTop);
            const size_t first = firstIt == m_offsets.begin() ? 0 : size_t(firstIt - m_offsets.begin()) - 1;
            const auto endIt = std::lower_bound(firstIt, m_offsets.end(), viewBottom);
            const size_t end = std::min(m_logs.size(), size_t(endIt - m_offsets.begin()));

            if (m_offsets[first] > 0.0f)
                ImGui::Dummy(ImVec2(0.0f, m_offsets[first]));

            for (size_t i = first; i < end; i++)
                draw_log(m_logs[i]);

            const float tailHeight = m_offsets.back() - m_offsets[end];
            if (tailHeight > 0.0f)
                ImGui::Dummy(ImVec2(0.0f, tailHeight));
        }

        ImGui::PopStyleVar();

        if (m_scrollToBottom && m_autoScroll)
            ImGui::SetScrollHereY(1.0f);
        m_scrollToBottom = false;
        m_scrollY = ImGui::GetScrollY();

        ImGui::EndChild();
        ImGui::End();
    }
} // namespace pe
