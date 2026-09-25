#include "ScriptEditor.h"
#include "Base/Process.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Script/ScriptSystem.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui.h"
#include <nlohmann/json.hpp>

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

    void ScriptEditor::SetTargetNode(NodeId *node)
    {
        m_targetNode = {};
        if (auto *renderer = GetGlobalSystem<RendererSystem>())
            m_targetNode = renderer->GetScene().MakeHandle(node);
        if (!m_build.valid())
            m_buildOutput.clear();
    }

    void ScriptEditor::OpenScript(NodeId *node, const std::string &path)
    {
        SetTargetNode(node);
        m_onSavedPath = nullptr;
        m_isNewScript = false;

        // The stored path is project-relative (e.g. "Assets/Scripts/x.lua"); a raw file open against it
        // fails because the CWD isn't the project root. The node's script instance already resolved it
        // to an absolute path — prefer that. Fall back to resolving "Assets/..." against the assets root.
        std::string resolved = path;
        if (IsCppScriptPath(path))
        {
            if (auto *ss = GetGlobalSystem<ScriptSystem>())
                if (const auto source = ss->CppSourceFile(path); !source.empty())
                    resolved = source;
        }
        if (!IsCppScriptPath(path))
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
        SetTargetNode(nullptr); // in-place edit: Save must NOT attach this to a node's Component_Script
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
        m_isCpp = false;
        m_editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
        SetTargetNode(node);
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
        m_isCpp = false;
        m_editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
        SetTargetNode(node);
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
        m_isCpp = false;
        m_editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
        SetTargetNode(nullptr); // zone scripts are owned by the zone, never a node's Component_Script
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
        m_isCpp = IsCppScriptPath(path);
        m_showFunctions = false;
        m_editor.SetLanguageDefinition(m_isCpp ? TextEditor::LanguageDefinition::CPlusPlus() : TextEditor::LanguageDefinition::Lua());
        m_editor.SetText("");
        m_originalSource.clear();
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open())
        {
            PE_WARN("[ScriptEditor] Cannot open '%s'", path.c_str());
            m_buildOutput = "Cannot open source file: " + path;
            m_loadedPath.clear();
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
        if (m_build.valid())
            return;
        if (!m_isNewScript && m_loadedPath.empty())
        {
            m_buildOutput = "No source file is open. Locate/import the C++ source first.";
            return;
        }
        std::string name(m_scriptNameBuf);
        if (name.empty() || name == "Undefined")
        {
            PE_WARN("[ScriptEditor] Enter a script name before saving.");
            return;
        }

        std::filesystem::path outPath = m_loadedPath;
        std::string content = m_editor.GetText();
        if (m_isNewScript)
        {
            std::filesystem::path scriptsDir = Path::Assets + "Scripts";
            if (m_isCpp)
            {
                if (!(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_') ||
                    name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
                {
                    m_buildOutput = "Use a C++ identifier for the new script name.";
                    return;
                }
                try
                {
                    std::ifstream config(std::filesystem::path(Path::Executable) / "NativeScripts.json");
                    scriptsDir = nlohmann::json::parse(config).at("sources").get<std::string>();
                }
                catch (const std::exception &e)
                {
                    m_buildOutput = std::string("Native build configuration unavailable: ") + e.what();
                    return;
                }
                for (size_t pos = 0; (pos = content.find("NewCppScript", pos)) != std::string::npos; pos += name.size())
                    content.replace(pos, 12, name);
            }
            std::filesystem::create_directories(scriptsDir);
            outPath = scriptsDir / (name + (m_isCpp ? ".cpp" : ".lua"));
            if (std::filesystem::exists(outPath))
            {
                m_buildOutput = "File already exists. Open it to edit: " + outPath.string();
                return;
            }
        }
        std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
        if (!f.is_open())
        {
            PE_WARN("[ScriptEditor] Cannot write '%s'", outPath.string().c_str());
            return;
        }
        f << content;
        f.close();
        if (!f)
        {
            m_buildOutput = "Failed to save: " + outPath.string();
            return;
        }

        if (m_targetNode.nodeId)
        {
            if (auto *r = GetGlobalSystem<RendererSystem>())
            {
                Scene &scene = r->GetScene();
                if (m_targetNode.IsValid(scene))
                    scene.SetNodeScript(m_targetNode.nodeId, outPath.string());
                else
                    m_targetNode = {};
            }
        }

        // Zone script slot (Script / Physics section): point it at the saved file. Fires on every save so
        // a rename keeps the slot in sync; the callback re-validates the node/zone itself.
        if (m_onSavedPath)
            m_onSavedPath(outPath.string());

        m_loadedPath = outPath.string();
        m_originalSource = content;
        m_editor.SetText(content);
        m_modified = false;
        m_isNewScript = false;

        if (m_isCpp)
            BuildCppScripts();
        else
            EventSystem::PushEvent(EventType::CompileScripts);
    }

    void ScriptEditor::OpenNewCppScript(NodeId *node)
    {
        OpenNewScript(node);
        m_isCpp = true;
        m_editor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
        snprintf(m_scriptNameBuf, sizeof(m_scriptNameBuf), "NewCppScript");
        m_editor.SetText(R"(#include "ScriptModule.h"

class NewCppScript
{
public:
    NewCppScript(const phasma::ScriptApi &api, phasma::Node node) : world(api), node(node) {}
    void Update(double dt)
    {
        phasma::Vec3 position;
        if (world.Position(node, position))
        {
            position.x += static_cast<float>(dt);
            world.SetPosition(node, position);
        }
    }
private:
    phasma::World world;
    phasma::Node node;
};

PHASMA_NODE_SCRIPT(NewCppScript)
)");
        m_modified = true;
    }

    void ScriptEditor::ImportCppScript(NodeId *node, const std::string &path)
    {
        try
        {
            std::ifstream file(std::filesystem::path(Path::Executable) / "NativeScripts.json");
            const auto directory = std::filesystem::path(nlohmann::json::parse(file).at("sources").get<std::string>());
            const auto source = std::filesystem::weakly_canonical(path);
            auto relative = source.lexically_relative(std::filesystem::weakly_canonical(directory));
            auto destination = source;
            if (relative.empty() || *relative.begin() == "..")
            {
                destination = directory / source.filename();
                std::filesystem::copy_file(source, destination); // Fail on collisions, never overwrite another script.
            }
            OpenScript(node, destination.string());
            SaveScript();
        }
        catch (const std::exception &e)
        {
            m_buildOutput = std::string("Cannot import C++ script: ") + e.what();
            m_open = true;
        }
    }

    void ScriptEditor::BuildCppScripts()
    {
        try
        {
            std::ifstream file(std::filesystem::path(Path::Executable) / "NativeScripts.json");
            const auto config = nlohmann::json::parse(file);
            const auto executable = config.at("cmake").get<std::string>();
            const std::vector<std::string> args{"--build", config.at("build").get<std::string>(),
                                                "--config", config.at("config").get<std::string>(), "--target", "PhasmaProjectNative", "--parallel", "2"};
            m_buildOutput = "Building C++ scripts...";
            m_build = std::async(std::launch::async, [executable, args]
                                 {
                ProcessOptions options;
                options.captureOutput = true;
                const auto result = RunProcess(executable, args, options);
                return std::string(result.started && result.exitCode == 0 ? "Build succeeded. Live reload will apply it.\n" : "Build failed. Previous compiled code remains active.\n") + result.output; });
        }
        catch (const std::exception &e)
        {
            m_buildOutput = std::string("Cannot build native scripts: ") + e.what();
        }
    }

    void ScriptEditor::Update()
    {
        if (m_build.valid() && m_build.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            try
            {
                m_buildOutput = m_build.get();
            }
            catch (const std::exception &e)
            {
                m_buildOutput = e.what();
            }
        }
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
        ImGui::BeginDisabled(!m_isNewScript);
        bool enterPressed = ImGui::InputText("##scriptname", m_scriptNameBuf, sizeof(m_scriptNameBuf),
                                             ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::EndDisabled();
        ui::ItemTooltip(m_isCpp ? "C++ source filename in the native scripts directory." : "Script filename to save under Assets/Scripts.");
        ImGui::SameLine();

        ImGui::BeginDisabled(m_build.valid());
        bool saveClicked = ImGui::Button(m_isCpp ? "Save & Build" : "Save");
        ImGui::EndDisabled();
        ui::ItemTooltip(m_isCpp ? "Save C++ source, build, and live reload on success." : "Save and reload Lua scripts.");
        ImGui::SameLine();

        if (!m_isCpp && ImGui::Button(m_showFunctions ? "Hide Functions" : "Show Functions"))
        {
            m_showFunctions = !m_showFunctions;
            if (m_showFunctions)
                RefreshFunctionList();
        }
        if (!m_isCpp)
            ui::ItemTooltip("Show Lua functions available to scripts.");

        if (m_modified)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.f, 0.75f, 0.3f, 1.f), "(modified)");
        }

        if (saveClicked || enterPressed)
            SaveScript();

        ImGui::Separator();
        if (!m_loadedPath.empty())
            ImGui::TextWrapped("%s", m_loadedPath.c_str());
        if (!m_buildOutput.empty() && ImGui::CollapsingHeader("Build / Save Output", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::BeginChild("##build_output", ImVec2(0, 110), true, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(m_buildOutput.c_str());
            ImGui::EndChild();
        }

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
