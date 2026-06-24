#include "ScriptEditor.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Script/ScriptSystem.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>

namespace pe
{
    namespace
    {
        bool ContainsInsensitive(const std::string &text, const char *filter)
        {
            if (!filter || filter[0] == '\0')
                return true;

            const std::string needle(filter);
            return std::search(text.begin(), text.end(), needle.begin(), needle.end(),
                               [](char a, char b)
                               {
                                   return std::tolower(static_cast<unsigned char>(a)) ==
                                          std::tolower(static_cast<unsigned char>(b));
                               }) != text.end();
        }
    } // namespace

    ScriptEditor::ScriptEditor() : Widget("Script Editor")
    {
        m_open = false; // only shows when explicitly opened
        m_editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
        ApplyVSCodePalette();
        m_editor.SetShowWhitespaces(false);
    }

    void ScriptEditor::ApplyVSCodePalette()
    {
        // VS Code Dark+ — same encoding as ProfilerWidget's vscode palette (0xAABBGGRR)
        static const TextEditor::Palette vscode = {{
            0xFFD4D4D4, // None
            0xFF569CD6, // Keyword
            0xFFB5CEA8, // Number
            0xFFCE9178, // String
            0xFFCE9178, // CharLiteral
            0xFFD4D4D4, // Punctuation
            0xFF9CDCFE, // Preprocessor
            0xFFD4D4D4, // Identifier
            0xFFDCDCAA, // KnownIdentifier
            0xFFDCDCAA, // PreprocIdentifier
            0xFF57A64A, // Comment (single line)
            0xFF57A64A, // Comment (multi line)
            0xFF1E1E1E, // Background
            0xFFD4D4D4, // Cursor
            0x40569CD6, // Selection
            0x800000FF, // ErrorMarker
            0x40F08000, // Breakpoint
            0xFF858585, // LineNumber
            0x40000000, // CurrentLineFill
            0x40808080, // CurrentLineFillInactive
            0x40A0A0A0, // CurrentLineEdge
        }};
        m_editor.SetPalette(vscode);
    }

    void ScriptEditor::OpenScript(NodeId *node, const std::string &path)
    {
        m_targetNode = node;
        m_onSavedPath = nullptr;
        m_isNewScript = false;

        // The stored path is project-relative (e.g. "Assets/Scripts/x.lua"); a raw file open against it
        // fails because the CWD isn't the project root. The node's script instance already resolved it
        // to an absolute path — prefer that. Fall back to resolving "Assets/..." against the assets root.
        std::string resolved = path;
        if (auto *ss = GetGlobalSystem<ScriptSystem>())
            if (NodeScriptInstance *inst = ss->FindNodeInstance(node); inst && !inst->path.empty())
                resolved = inst->path;
        if (!std::filesystem::exists(resolved) && resolved.rfind("Assets/", 0) == 0)
            resolved = Path::Assets + resolved.substr(7); // strip leading "Assets/" (7 chars)

        m_loadedPath = resolved;
        std::filesystem::path p(resolved);
        snprintf(m_scriptNameBuf, sizeof(m_scriptNameBuf), "%s", p.stem().string().c_str());

        LoadScriptFile(resolved);
        m_open = true;
    }

    void ScriptEditor::OpenScriptFile(const std::string &path)
    {
        m_targetNode = nullptr; // in-place edit: Save must NOT attach this to a node's Component_Script
        m_onSavedPath = nullptr;
        m_isNewScript = false;
        std::string resolved = path;
        if (!std::filesystem::exists(resolved) && resolved.rfind("Assets/", 0) == 0)
            resolved = Path::Assets + resolved.substr(7);
        m_loadedPath = resolved;
        std::filesystem::path p(resolved);
        snprintf(m_scriptNameBuf, sizeof(m_scriptNameBuf), "%s", p.stem().string().c_str());
        LoadScriptFile(resolved);
        m_open = true;
    }

    void ScriptEditor::OpenNewScript(NodeId *node)
    {
        m_targetNode = node;
        m_onSavedPath = nullptr;
        m_isNewScript = true;
        m_loadedPath = "";
        snprintf(m_scriptNameBuf, sizeof(m_scriptNameBuf), "Undefined");
        m_editor.SetText("");
        m_originalSource = "";
        m_modified = false;
        m_pendingFocusName = true;
        m_open = true;
    }

    void ScriptEditor::OpenNewScriptWithContent(NodeId *node, const std::string &nameHint, const std::string &content)
    {
        m_targetNode = node;
        m_onSavedPath = nullptr;
        m_isNewScript = true;
        m_loadedPath = "";
        snprintf(m_scriptNameBuf, sizeof(m_scriptNameBuf), "%s", nameHint.empty() ? "trigger" : nameHint.c_str());
        m_editor.SetText(content);
        m_originalSource = ""; // differs from content -> shows as modified/unsaved
        m_modified = true;
        m_pendingFocusName = true;
        m_open = true;
    }

    void ScriptEditor::OpenNewScriptForPath(const std::string &nameHint, const std::string &content,
                                            std::function<void(const std::string &)> onSaved)
    {
        m_targetNode = nullptr; // zone scripts are owned by the zone, never a node's Component_Script
        m_onSavedPath = std::move(onSaved);
        m_isNewScript = true;
        m_loadedPath = "";
        snprintf(m_scriptNameBuf, sizeof(m_scriptNameBuf), "%s", nameHint.empty() ? "zone_script" : nameHint.c_str());
        m_editor.SetText(content);
        m_originalSource = ""; // differs from content -> shows as modified/unsaved
        m_modified = true;
        m_pendingFocusName = true;
        m_open = true;
    }

    void ScriptEditor::LoadScriptFile(const std::string &path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open())
        {
            PE_WARN("[ScriptEditor] Cannot open '%s'", path.c_str());
            return;
        }
        std::string content((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
        m_originalSource = content;
        m_editor.SetText(content);
        m_modified = false;
    }

    void ScriptEditor::SaveScript()
    {
        std::string name(m_scriptNameBuf);
        if (name.empty() || name == "Undefined")
        {
            PE_WARN("[ScriptEditor] Enter a script name before saving.");
            return;
        }

        std::filesystem::path scriptsDir = Path::Assets + "Scripts";
        std::filesystem::create_directories(scriptsDir);

        std::filesystem::path outPath = scriptsDir / (name + ".lua");

        std::string content = m_editor.GetText();
        std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
        if (!f.is_open())
        {
            PE_WARN("[ScriptEditor] Cannot write '%s'", outPath.string().c_str());
            return;
        }
        f << content;
        f.close();

        if (m_targetNode)
        {
            if (auto *r = GetGlobalSystem<RendererSystem>())
            {
                Scene &scene = r->GetScene();
                if (scene.IsNodeAlive(m_targetNode))
                    scene.SetNodeScript(m_targetNode, outPath.string());
                else
                    m_targetNode = nullptr;
            }
        }

        // Zone script slot (Script / Physics section): point it at the saved file. Fires on every save so
        // a rename keeps the slot in sync; the callback re-validates the node/zone itself.
        if (m_onSavedPath)
            m_onSavedPath(outPath.string());

        m_loadedPath = outPath.string();
        m_originalSource = content;
        m_modified = false;
        m_isNewScript = false;

        EventSystem::PushEvent(EventType::CompileScripts);
    }

    void ScriptEditor::Update()
    {
        if (!m_open)
            return;

        ImGui::SetNextWindowSize(ImVec2(820.f, 620.f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Script Editor", &m_open))
        {
            ImGui::End();
            return;
        }

        // ── Name field ────────────────────────────────────────────────────────────
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Name:");
        ImGui::SameLine();

        if (m_pendingFocusName)
        {
            ImGui::SetKeyboardFocusHere();
            m_pendingFocusName = false;
        }

        ImGui::SetNextItemWidth(280.f);
        bool enterPressed = ImGui::InputText("##scriptname", m_scriptNameBuf, sizeof(m_scriptNameBuf),
                                             ImGuiInputTextFlags_EnterReturnsTrue);
        ui::ItemTooltip("Script filename to save under Assets/Scripts.");
        ImGui::SameLine();

        bool saveClicked = ImGui::Button("Save");
        ui::ItemTooltip("Save the script and compile Lua scripts.");
        ImGui::SameLine();

        if (ImGui::Button(m_showFunctions ? "Hide Functions" : "Show Functions"))
        {
            m_showFunctions = !m_showFunctions;
            if (m_showFunctions)
                RefreshFunctionList();
        }
        ui::ItemTooltip("Show Lua functions available to scripts.");

        if (m_modified)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.f, 0.75f, 0.3f, 1.f), "(modified)");
        }

        if (saveClicked || enterPressed)
            SaveScript();

        ImGui::Separator();

        if (m_showFunctions)
            DrawFunctionBrowser();

        // ── Editor ───────────────────────────────────────────────────────────────
        float availH = ImGui::GetContentRegionAvail().y;

        ImGui::BeginChild("##script_editor_child", ImVec2(-1.f, availH), false);

        if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl)
        {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.f)
                m_editorFontScale = std::clamp(m_editorFontScale + wheel * 0.05f, 0.5f, 2.0f);
        }
        ImGui::SetWindowFontScale(m_editorFontScale);

        m_editor.Render("##script_editor", ImVec2(-1.f, -1.f));
        if (m_editor.IsTextChanged())
            m_modified = (m_editor.GetText() != m_originalSource);

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
            ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
            SaveScript();

        ImGui::EndChild();

        ImGui::End();
    }

    void ScriptEditor::RefreshFunctionList()
    {
        m_luaFunctions.clear();
        if (ScriptSystem *scriptSystem = GetGlobalSystem<ScriptSystem>())
            m_luaFunctions = scriptSystem->ListLuaFunctions();

        RebuildFunctionListText();
    }

    void ScriptEditor::RebuildFunctionListText()
    {
        m_luaFunctionsText.clear();
        if (m_luaFunctions.empty())
        {
            m_luaFunctionsText = "No Lua functions available. Make sure the ScriptSystem is initialized.";
        }
        else
        {
            for (const std::string &functionName : m_luaFunctions)
            {
                m_luaFunctionsText += functionName;
                m_luaFunctionsText += '\n';
            }
        }

        m_luaFunctionsTextBuffer.assign(m_luaFunctionsText.begin(), m_luaFunctionsText.end());
        m_luaFunctionsTextBuffer.push_back('\0');
    }

    void ScriptEditor::DrawFunctionBrowser()
    {
        if (m_luaFunctionsTextBuffer.empty())
            RefreshFunctionList();

        const float availableHeight = ImGui::GetContentRegionAvail().y;
        const float panelHeight = std::clamp(availableHeight * 0.34f, 170.f, 260.f);
        if (!ImGui::BeginChild("##script_function_browser", ImVec2(-1.f, panelHeight), true))
        {
            ImGui::EndChild();
            return;
        }

        ImGui::Text("Lua Functions (%zu)", m_luaFunctions.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh"))
            RefreshFunctionList();
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy All"))
            ImGui::SetClipboardText(m_luaFunctionsText.c_str());

        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.f);
        ImGui::InputTextWithHint("##lua_function_filter", "Filter", m_functionFilterBuf, sizeof(m_functionFilterBuf));

        ImGui::Separator();

        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float rowListWidth = std::clamp(availableWidth * 0.45f, 280.f, 420.f);
        ImGui::BeginChild("##lua_function_rows", ImVec2(rowListWidth, -1.f), true);
        for (int i = 0; i < static_cast<int>(m_luaFunctions.size()); ++i)
        {
            const std::string &functionName = m_luaFunctions[i];
            if (!ContainsInsensitive(functionName, m_functionFilterBuf))
                continue;

            ImGui::PushID(i);
            if (ImGui::SmallButton("Copy"))
                ImGui::SetClipboardText(functionName.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Insert"))
            {
                m_editor.InsertText(functionName + "\n");
                m_modified = (m_editor.GetText() != m_originalSource);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(functionName.c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();

        ImGui::SameLine();
        if (!m_luaFunctionsTextBuffer.empty())
        {
            ImGui::InputTextMultiline("##lua_function_text",
                                      m_luaFunctionsTextBuffer.data(),
                                      m_luaFunctionsTextBuffer.size(),
                                      ImVec2(-1.f, -1.f),
                                      ImGuiInputTextFlags_ReadOnly);
        }

        ImGui::EndChild();
    }
} // namespace pe
