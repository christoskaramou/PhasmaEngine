#include "ScriptEditor.h"
#include "Base/EventSystem.h"
#include "Base/Path.h"
#include "GUI/GUI.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui.h"

namespace pe
{
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
        m_isNewScript = false;
        m_loadedPath = path;

        std::filesystem::path p(path);
        snprintf(m_scriptNameBuf, sizeof(m_scriptNameBuf), "%s", p.stem().string().c_str());

        LoadScriptFile(path);
        m_open = true;
    }

    void ScriptEditor::OpenNewScript(NodeId *node)
    {
        m_targetNode = node;
        m_isNewScript = true;
        m_loadedPath = "";
        snprintf(m_scriptNameBuf, sizeof(m_scriptNameBuf), "Undefined");
        m_editor.SetText("");
        m_originalSource = "";
        m_modified = false;
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
        ImGui::SameLine();

        bool saveClicked = ImGui::Button("Save");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Save script  (Ctrl+S)");

        if (m_modified)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.f, 0.75f, 0.3f, 1.f), "(modified)");
        }

        if (saveClicked || enterPressed)
            SaveScript();

        ImGui::Separator();

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
} // namespace pe
