#pragma once
#include "GUI/Widget.h"
#include "GUI/AI/AICompletionService.h"
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

        void SetCompletionService(AICompletionService *svc) { m_completion = svc; }

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

        AICompletionService *m_completion = nullptr;
        std::string m_ghostText;
        bool m_completionPending = false;
        TextEditor::Coordinates m_savedCursor;
    };
} // namespace pe
