#pragma once
#include "GUI/Widget.h"
#include "Scene/SceneNodeHandle.h"
#include "Script/CppScript.h"
#include "Script/DetachedTask.h"
#include "TextEditor.h"

#include <chrono>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace pe
{
    class NodeId;

    class ScriptEditor : public Widget
    {
    public:
        ScriptEditor();
        void Update() override;

        // Open an existing Lua or C++ source file attached to a node
        void OpenScript(NodeId *node, const std::string &path);

        // Open a .lua file for in-place editing with NO node attach (used for trigger-zone scripts,
        // which are owned by the zone, not a node's Component_Script). Save overwrites the file only.
        void OpenScriptFile(const std::string &path);

        // Create a new empty script for a node (name defaults to "Undefined")
        void OpenNewScript(NodeId *node);
        void OpenNewCppScript(NodeId *node);
        void ImportCppScript(NodeId *node, const std::string &path);

        // Create a new script for a node pre-filled with content (e.g. a trigger template) and a
        // suggested filename. The editor opens so the user can rename/review, then Save writes it
        // under Assets/Scripts and attaches it to the node.
        void OpenNewScriptWithContent(NodeId *node, const std::string &nameHint, const std::string &content);

        // Create a new script pre-filled with content, NOT attached to any node's Component_Script.
        // On each Save, `onSaved` is called with the written absolute path so the caller can point a
        // trigger-zone script slot at it. Used for the zone's Script / Physics section scripts.
        void OpenNewScriptForPath(const std::string &nameHint, const std::string &content,
                                  std::function<void(const std::string &)> onSaved);

    private:
        void SaveScript();
        void LoadScriptFile(const std::string &path);
        void ApplyVSCodePalette();
        void DrawFunctionBrowser();
        void RefreshFunctionList();
        void RebuildFunctionListText();
        void BuildCppScripts();
        void PollReload();
        void SetTargetNode(NodeId *node);

        SceneNodeHandle m_targetNode;
        std::function<void(const std::string &)> m_onSavedPath; // set by OpenNewScriptForPath; fired in SaveScript
        char m_scriptNameBuf[256] = "Undefined";
        char m_functionFilterBuf[128] = "";
        std::string m_loadedPath; // absolute path of file on disk; empty = new/unsaved
        std::string m_originalSource;
        std::string m_luaFunctionsText;
        std::vector<std::string> m_luaFunctions;
        std::vector<char> m_luaFunctionsTextBuffer;
        bool m_isNewScript = false;
        bool m_modified = false;
        bool m_pendingFocusName = false;
        bool m_showFunctions = false;
        bool m_isCpp = false;
        std::string m_buildOutput;
        DetachedTask<std::pair<bool, std::string>> m_build; // never joined: quitting mid-build doesn't wait
        CppScriptStatus m_buildBaseline;                    // module status when the build started
        std::string m_buildLog;
        std::chrono::steady_clock::time_point m_reloadDeadline;
        bool m_awaitingReload = false;

        TextEditor m_editor;
        float m_editorFontScale = 1.0f;
    };
} // namespace pe
