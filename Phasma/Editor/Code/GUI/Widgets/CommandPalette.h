#pragma once
#include "GUI/Widget.h"

struct ImGuiInputTextCallbackData;

namespace pe
{
    // Ctrl+P: fuzzy search over the same editor actions MCP agents invoke (GUI::QueryEditorActions).
    class CommandPalette : public Widget
    {
    public:
        CommandPalette() : Widget("Command Palette") { m_open = false; }
        void Open();
        void Update() override;

    private:
        struct Entry
        {
            std::string id;
            std::string label;
            std::string category;
            std::string haystack; // lowercased "category label id"
            bool enabled = true;
            bool hasChecked = false;
            bool checked = false;
        };

        void Refresh();
        void Filter();
        void Run(const Entry &entry);
        void DrawResults(bool &run);
        static int InputCallback(ImGuiInputTextCallbackData *data);

        std::vector<Entry> m_entries;
        std::vector<int> m_matches;
        char m_query[128] = {};
        int m_cursor = 0;
        bool m_cursorMoved = false;
        bool m_justOpened = false;
    };
} // namespace pe
