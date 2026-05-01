#pragma once
#include "GUI/Widget.h"
#include "TextEditor.h"

namespace pe
{
    class NodeId;

    class ScriptEditor : public Widget
    {
    public:
        ScriptEditor();
        void Update() override;

        // Open an existing .lua file attached to a node
        void OpenScript(NodeId *node, const std::string &path);

        // Create a new empty script for a node (name defaults to "Undefined")
        void OpenNewScript(NodeId *node);

    private:
        void SaveScript();
        void LoadScriptFile(const std::string &path);
        void ApplyVSCodePalette();

        NodeId *m_targetNode = nullptr;
        char m_scriptNameBuf[256] = "Undefined";
        std::string m_loadedPath; // absolute path of file on disk; empty = new/unsaved
        std::string m_originalSource;
        bool m_isNewScript = false;
        bool m_modified = false;
        bool m_pendingFocusName = false;

        TextEditor m_editor;
        float m_editorFontScale = 1.0f;
    };
} // namespace pe
