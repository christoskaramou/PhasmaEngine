#include "Console.h"

#include "GUI/GUIState.h"
#include "GUI/Helpers.h"
#include "Script/ScriptSystem.h"

namespace pe
{
    namespace
    {
        constexpr size_t kMaxLogs = 10000;
        constexpr size_t kTrimLogs = kMaxLogs / 10; // trim in batches so the erase is paid once per kTrimLogs lines
        constexpr int kMaxSuggestionRows = 8;
        constexpr const char *kCommandsCmd = "/commands";

        bool IsTokenChar(char c)
        {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == ':';
        }

        // Start of the word being completed: the whole line for a /command, else the trailing Lua name.
        size_t TokenStart(const char *text, size_t len)
        {
            if (len > 0 && text[0] == '/')
                return 0;

            size_t start = len;
            while (start > 0 && IsTokenChar(text[start - 1]))
                --start;

            return start;
        }

        size_t FindInsensitive(std::string_view text, std::string_view needle)
        {
            const auto it = std::search(text.begin(),
                                        text.end(),
                                        needle.begin(),
                                        needle.end(),
                                        [](char a, char b)
                                        {
                                            return std::tolower(static_cast<unsigned char>(a)) ==
                                                   std::tolower(static_cast<unsigned char>(b));
                                        });
            return it == text.end() ? std::string::npos : size_t(it - text.begin());
        }

        std::string CompleteLine(const char *line, const std::string &name)
        {
            const size_t len = std::strlen(line);
            return std::string(line, TokenStart(line, len)) + name;
        }
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
        if (m_commandsOpen)
            DrawCommandsWindow();

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

        UpdateMatches();
        const bool showSuggestions = !m_matches.empty() && (m_inputActive || m_suggestHovered);
        const float suggestionsHeight = showSuggestions ? SuggestionsHeight() : 0.0f;

        DrawLogs(copy, suggestionsHeight);
        if (showSuggestions)
            DrawSuggestions(suggestionsHeight);
        else
            m_suggestHovered = false;

        DrawInput();
        ImGui::End();
    }

    void Console::DrawLogs(bool copy, float reservedHeight)
    {
        // Held only while drawing: Lua run from the input line logs through AddLog, which takes this lock too.
        std::lock_guard<std::mutex> lock(m_mutex);

        // Keep the same entries in view after TrimLogs removed lines above them
        if (m_trimmedHeight > 0.0f && !m_filter.IsActive())
            ImGui::SetNextWindowScroll(ImVec2(-1.0f, std::max(0.0f, m_scrollY - m_trimmedHeight)));
        m_trimmedHeight = 0.0f;

        const float inputHeight = ImGui::GetFrameHeightWithSpacing() + reservedHeight;
        ImGui::BeginChild("scrolling", ImVec2(0, -inputHeight), false, 0);

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
    }

    int Console::InputCallback(ImGuiInputTextCallbackData *data)
    {
        auto *self = static_cast<Console *>(data->UserData);
        const int matchCount = static_cast<int>(self->m_matches.size());

        if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
        {
            if (matchCount == 0)
                return 0;

            const int pick = self->m_matches[std::max(0, self->m_matchSel)];
            const std::string line = CompleteLine(data->Buf, self->m_functions[pick]);
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, line.c_str());
            return 0;
        }

        // Up/Down walk the suggestion list while it is open, the history otherwise
        if (matchCount > 0)
        {
            if (data->EventKey == ImGuiKey_UpArrow)
                self->m_matchSel = self->m_matchSel <= 0 ? matchCount - 1 : self->m_matchSel - 1;
            else
                self->m_matchSel = (self->m_matchSel + 1) % matchCount;

            self->m_scrollToMatch = true;
            return 0;
        }

        if (self->m_history.empty())
            return 0;

        const int count = static_cast<int>(self->m_history.size());
        const int prev = self->m_historyPos;
        if (data->EventKey == ImGuiKey_UpArrow)
            self->m_historyPos = self->m_historyPos < 0 ? count - 1 : std::max(0, self->m_historyPos - 1);
        else if (data->EventKey == ImGuiKey_DownArrow && self->m_historyPos >= 0)
            self->m_historyPos = self->m_historyPos + 1 < count ? self->m_historyPos + 1 : -1;

        if (prev == self->m_historyPos)
            return 0;

        const char *line = self->m_historyPos >= 0 ? self->m_history[self->m_historyPos].c_str() : "";
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, line);
        return 0;
    }

    void Console::DrawInput()
    {
        if (m_refocusInput)
        {
            ImGui::SetKeyboardFocusHere();
            m_refocusInput = false;
        }

        const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                          ImGuiInputTextFlags_EscapeClearsAll |
                                          ImGuiInputTextFlags_CallbackHistory |
                                          ImGuiInputTextFlags_CallbackCompletion;
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool submitted = ImGui::InputTextWithHint("##lua",
                                                        "Lua  (Tab completes, /commands lists all, =expr prints a value)",
                                                        m_input,
                                                        sizeof(m_input),
                                                        flags,
                                                        &Console::InputCallback,
                                                        this);

        if (ImGui::IsItemActivated())
        {
            auto *scripts = GetGlobalSystem<ScriptSystem>();
            m_functions = scripts ? scripts->ListLuaFunctions() : std::vector<std::string>{};
            m_functions.insert(m_functions.begin(), kCommandsCmd);
            m_matchedText.clear(); // re-match against the fresh list
        }

        m_inputActive = ImGui::IsItemActive();

        // Enter on a highlighted suggestion completes it instead of running the line
        if (submitted && m_matchSel >= 0 && m_matchSel < static_cast<int>(m_matches.size()))
        {
            const std::string line = CompleteLine(m_input, m_functions[m_matches[m_matchSel]]);
            std::snprintf(m_input, sizeof(m_input), "%s", line.c_str());
            m_refocusInput = true;
        }
        else if (submitted)
        {
            ExecuteInput(m_input);
            m_input[0] = 0;
            m_refocusInput = true;
        }

        ui::ItemTooltip("Run Lua in the editor's script state, e.g. =scene.get_node_count() or pe_log(\"hi\").");
    }

    void Console::UpdateMatches()
    {
        if (m_matchedText == m_input)
            return;

        m_matchedText = m_input;
        m_matches.clear();
        m_matchSel = -1;

        // A line recalled from history is left alone so Up/Down keep walking the history
        const bool recalled = m_historyPos >= 0 && m_history[m_historyPos] == m_matchedText;
        if (!recalled)
            m_historyPos = -1;

        const size_t start = TokenStart(m_input, m_matchedText.size());
        const std::string_view token = std::string_view(m_matchedText).substr(start);
        if (recalled || token.empty())
            return;

        std::vector<int> contains;
        for (int i = 0; i < static_cast<int>(m_functions.size()); i++)
        {
            const std::string &name = m_functions[i];
            const bool slashName = name[0] == '/';
            if (slashName != (token[0] == '/'))
                continue;

            const size_t pos = FindInsensitive(name, token);
            if (pos == std::string::npos)
                continue;

            if (pos == 0)
                m_matches.push_back(i);
            else
                contains.push_back(i);
        }
        m_matches.insert(m_matches.end(), contains.begin(), contains.end());

        // Nothing left to complete once the word is the only, exact match
        const bool exact = m_matches.size() == 1 && m_functions[m_matches[0]].size() == token.size();
        if (exact)
            m_matches.clear();
    }

    float Console::SuggestionsHeight() const
    {
        const int rows = std::min(static_cast<int>(m_matches.size()), kMaxSuggestionRows);
        return rows * ImGui::GetTextLineHeightWithSpacing() +
               ImGui::GetStyle().WindowPadding.y * 2.0f +
               ImGui::GetStyle().ItemSpacing.y;
    }

    void Console::DrawSuggestions(float height)
    {
        ImGui::BeginChild("##lua_suggest", ImVec2(0, height - ImGui::GetStyle().ItemSpacing.y), true);
        m_suggestHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

        for (int i = 0; i < static_cast<int>(m_matches.size()); i++)
        {
            const bool selected = i == m_matchSel;
            ImGui::PushID(i);

            // Clicking moves focus off the input, so its buffer can be written directly here
            if (ImGui::Selectable(m_functions[m_matches[i]].c_str(), selected))
            {
                const std::string line = CompleteLine(m_input, m_functions[m_matches[i]]);
                std::snprintf(m_input, sizeof(m_input), "%s", line.c_str());
                m_refocusInput = true;
            }

            if (selected && m_scrollToMatch)
                ImGui::SetScrollHereY();

            ImGui::PopID();
        }

        m_scrollToMatch = false;
        ImGui::EndChild();
    }

    void Console::ListCommands(const std::string &filter)
    {
        auto *scripts = GetGlobalSystem<ScriptSystem>();
        if (!scripts)
        {
            AddLog(LogType::Error, "ScriptSystem not available");
            return;
        }

        m_commandList = scripts->ListLuaFunctions();
        std::snprintf(m_commandsFilter, sizeof(m_commandsFilter), "%s", filter.c_str());
        m_commandsOpen = true;
        ImGui::SetWindowFocus("Lua Commands");
    }

    void Console::DrawCommandsWindow()
    {
        ImGui::SetNextWindowSize(ImVec2(420.0f, 480.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Lua Commands", &m_commandsOpen))
        {
            ImGui::End();
            return;
        }

        if (ImGui::Button("Refresh"))
        {
            if (auto *scripts = GetGlobalSystem<ScriptSystem>())
                m_commandList = scripts->ListLuaFunctions();
        }
        ui::ItemTooltip("Re-read the functions from the script state.");

        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##commands_filter", "Filter", m_commandsFilter, sizeof(m_commandsFilter));

        ImGui::Separator();
        ImGui::BeginChild("##commands_list");

        // Clicking a name puts it in the console's Lua line, replacing the word being typed there
        for (int i = 0; i < static_cast<int>(m_commandList.size()); i++)
        {
            const std::string &name = m_commandList[i];
            const bool filtered = m_commandsFilter[0] != 0 &&
                                  FindInsensitive(name, m_commandsFilter) == std::string::npos;
            if (filtered)
                continue;

            ImGui::PushID(i);
            if (ImGui::Selectable(name.c_str()))
            {
                const std::string line = CompleteLine(m_input, name);
                std::snprintf(m_input, sizeof(m_input), "%s", line.c_str());
                m_open = true;
                m_refocusInput = true;
                ImGui::SetWindowFocus(m_name.c_str());
            }
            ImGui::PopID();
        }

        ImGui::EndChild();
        ImGui::End();
    }

    void Console::ExecuteInput(std::string code)
    {
        const size_t first = code.find_first_not_of(" \t");
        if (first == std::string::npos)
            return;
        code = code.substr(first);

        m_history.erase(std::remove(m_history.begin(), m_history.end(), code), m_history.end());
        m_history.push_back(code);
        m_historyPos = -1;
        AddLog(LogType::Info, "> %s", code.c_str());

        if (code[0] == '/')
        {
            const size_t len = std::strlen(kCommandsCmd);
            const bool isCommands = code.compare(0, len, kCommandsCmd) == 0 &&
                                    (code.size() == len || code[len] == ' ');
            if (!isCommands)
            {
                AddLog(LogType::Warn, "Unknown command - try /commands");
                return;
            }

            const size_t arg = code.find_first_not_of(' ', len);
            ListCommands(arg == std::string::npos ? "" : code.substr(arg));
            return;
        }

        // "=expr" is the classic Lua REPL shorthand for "return expr"
        if (code[0] == '=')
            code = "return " + code.substr(1);

        auto *scripts = GetGlobalSystem<ScriptSystem>();
        if (!scripts)
        {
            AddLog(LogType::Error, "ScriptSystem not available");
            return;
        }

        const std::string result = scripts->ExecuteLua(code);
        if (result.empty())
            return;

        const bool failed = result.rfind("error:", 0) == 0;
        AddLog(failed ? LogType::Error : LogType::Info, "%s", result.c_str());
    }
} // namespace pe
