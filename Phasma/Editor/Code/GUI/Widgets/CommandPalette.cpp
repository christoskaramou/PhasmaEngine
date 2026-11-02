#include "CommandPalette.h"
#include "GUI/GUI.h"
#include "imgui/imgui.h"
#include <nlohmann/json.hpp>

namespace pe
{
    namespace
    {
        constexpr const char *kPopupId = "##CommandPalette";
        constexpr int kMaxVisibleRows = 12;

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c)
                           {
                               return static_cast<char>(std::tolower(c));
                           });
            return s;
        }

        // Actions that need arguments, return data only, or hide the whole editor GUI stay MCP-only.
        bool PaletteSkips(const std::string &id, const std::string &kind)
        {
            const bool needsArgs = id.rfind("voxelpainter.", 0) == 0;
            const bool hidesEditor = id == "editor.render.toggle";
            const bool isSelf = id == "edit.command_palette";
            return needsArgs || hidesEditor || isSelf || kind == "query";
        }

        // 0 = substring match, 1 = in-order subsequence match, -1 = no match.
        int MatchRank(const std::string &haystack, const std::string &query)
        {
            if (query.empty() || haystack.find(query) != std::string::npos)
                return 0;

            size_t pos = 0;
            for (char c : query)
            {
                if (c == ' ')
                    continue;

                pos = haystack.find(c, pos);
                if (pos == std::string::npos)
                    return -1;

                ++pos;
            }
            return 1;
        }
    } // namespace

    void CommandPalette::Open()
    {
        m_open = true;
        m_justOpened = true;
        m_query[0] = 0;
        Refresh();
        Filter();
    }

    void CommandPalette::Refresh()
    {
        m_entries.clear();
        if (!m_gui)
            return;

        const nlohmann::json j = nlohmann::json::parse(m_gui->QueryEditorActions(), nullptr, false);
        if (j.is_discarded() || !j.contains("actions") || !j["actions"].is_array())
            return;

        for (const auto &a : j["actions"])
        {
            Entry e;
            e.id = a.value("id", "");
            e.label = a.value("label", e.id);
            e.category = a.value("category", "");
            if (e.id.empty() || PaletteSkips(e.id, a.value("kind", "")))
                continue;

            e.enabled = a.value("enabled", true);
            e.hasChecked = a.contains("checked");
            e.checked = a.value("checked", false);
            e.haystack = Lower(e.category + " " + e.label + " " + e.id);
            m_entries.push_back(std::move(e));
        }
    }

    void CommandPalette::Filter()
    {
        const std::string query = Lower(m_query);
        std::vector<std::pair<int, int>> ranked; // (rank, entry index)
        for (int i = 0; i < static_cast<int>(m_entries.size()); i++)
        {
            const int rank = MatchRank(m_entries[i].haystack, query);
            if (rank >= 0)
                ranked.emplace_back(rank, i);
        }

        std::stable_sort(ranked.begin(), ranked.end(),
                         [](const auto &a, const auto &b)
                         {
                             return a.first < b.first;
                         });

        m_matches.clear();
        for (const auto &r : ranked)
            m_matches.push_back(r.second);

        m_cursor = 0;
        m_cursorMoved = true;
    }

    void CommandPalette::Run(const Entry &entry)
    {
        if (!m_gui || !entry.enabled)
            return;

        const nlohmann::json result = nlohmann::json::parse(m_gui->InvokeEditorAction(entry.id, "{}"), nullptr, false);
        if (!result.is_discarded() && result.contains("error"))
            PE_WARN("[Palette] %s: %s", entry.label.c_str(), result["error"].dump().c_str());
    }

    int CommandPalette::InputCallback(ImGuiInputTextCallbackData *data)
    {
        auto *self = static_cast<CommandPalette *>(data->UserData);
        if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory || self->m_matches.empty())
            return 0;

        const int last = static_cast<int>(self->m_matches.size()) - 1;
        if (data->EventKey == ImGuiKey_UpArrow)
            self->m_cursor = self->m_cursor > 0 ? self->m_cursor - 1 : last;
        else if (data->EventKey == ImGuiKey_DownArrow)
            self->m_cursor = self->m_cursor < last ? self->m_cursor + 1 : 0;

        self->m_cursorMoved = true;
        return 0;
    }

    void CommandPalette::DrawResults(bool &run)
    {
        const int rows = std::clamp(static_cast<int>(m_matches.size()), 1, kMaxVisibleRows);
        const float height = rows * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y;
        if (!ImGui::BeginChild("##results", ImVec2(0.0f, height)))
        {
            ImGui::EndChild();
            return;
        }

        if (m_matches.empty())
            ImGui::TextDisabled("No matching command");

        for (int i = 0; i < static_cast<int>(m_matches.size()); i++)
        {
            const Entry &e = m_entries[m_matches[i]];
            ImGui::PushID(i);
            ImGui::BeginDisabled(!e.enabled);

            if (ImGui::Selectable("##row", i == m_cursor))
            {
                m_cursor = i;
                run = true;
            }
            if (i == m_cursor && m_cursorMoved)
                ImGui::SetScrollHereY();

            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextDisabled("%s", e.category.c_str());
            ImGui::SameLine();
            ImGui::TextUnformatted(e.label.c_str());
            if (e.hasChecked && e.checked)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("on");
            }

            ImGui::EndDisabled();
            ImGui::PopID();
        }

        m_cursorMoved = false;
        ImGui::EndChild();
    }

    void CommandPalette::Update()
    {
        if (m_justOpened)
            ImGui::OpenPopup(kPopupId);

        const ImGuiViewport *vp = ImGui::GetMainViewport();
        const float width = std::min(560.0f, vp->WorkSize.x * 0.8f);
        const ImVec2 topCenter(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 48.0f);
        ImGui::SetNextWindowPos(topCenter, ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
        if (!ImGui::BeginPopup(kPopupId))
        {
            m_open = false;
            return;
        }

        if (m_justOpened)
            ImGui::SetKeyboardFocusHere();
        m_justOpened = false;

        ImGui::SetNextItemWidth(-FLT_MIN);
        const ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackHistory;
        const bool edited = ImGui::InputTextWithHint("##query",
                                                     "Type a command...",
                                                     m_query,
                                                     sizeof(m_query),
                                                     flags,
                                                     &CommandPalette::InputCallback,
                                                     this);
        if (edited)
            Filter();

        bool run = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        DrawResults(run);

        const bool valid = m_cursor >= 0 && m_cursor < static_cast<int>(m_matches.size());
        if (run && valid)
        {
            const Entry entry = m_entries[m_matches[m_cursor]];
            if (entry.enabled)
            {
                ImGui::CloseCurrentPopup();
                m_open = false;
                Run(entry);
            }
        }

        ImGui::EndPopup();
    }
} // namespace pe
