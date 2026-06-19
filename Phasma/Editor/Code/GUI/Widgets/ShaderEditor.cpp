// Phasma/Editor/Code/GUI/Widgets/ShaderEditor.cpp
#include "ShaderEditor.h"
#include "GUI/Helpers.h"
#include "imgui/imgui.h"

namespace pe
{
    static const TextEditor::Palette s_vscode = {{
        0xFFded9d6,
        0xFFcc9c56,
        0xFFa7cdb4,
        0xFF5485ce,
        0xFF5485ce,
        0xFFb1d4c9,
        0xFFb686c5,
        0xFFfedc9c,
        0xFFaadcdc,
        0xFFcc6e2a,
        0xFF55996a,
        0xFF55996a,
        0xFF1f1f1f,
        0xFFadafae,
        0x40784f26,
        0x804745c7,
        0xFF0014e5,
        0xFF747664,
        0x401f1f1f,
        0x201f1f1f,
        0x601f1f1f,
    }};

    static const TextEditor::Palette s_soft = {{
        0xFFF4D6CD,
        0xFFF7A6CB,
        0xFF87B3FA,
        0xFFA1E3A6,
        0xFFEBDC89,
        0xFFDEC2BA,
        0xFFEBDC89,
        0xFFF4D6CD,
        0xFFAFE2F9,
        0xFFFAB489,
        0xFF86706C,
        0xFF705B58,
        0xFF2E1E1E,
        0xFFE7C2F5,
        0x40443231,
        0x80A88BF3,
        0xFF87B3FA,
        0xFF86706C,
        0x40251818,
        0x20251818,
        0x605A4745,
    }};

    void ShaderEditor::ScanShaderFiles()
    {
        m_shaderFiles.clear();
        m_shaderRelPaths.clear();
        const std::string shaderDir = Path::Assets + "Shaders/";
        if (!std::filesystem::exists(shaderDir))
            return;
        for (auto &entry : std::filesystem::recursive_directory_iterator(shaderDir))
        {
            if (!entry.is_regular_file())
                continue;
            const auto &p = entry.path();
            if (p.extension() != ".hlsl" && p.extension() != ".glsl" && p.extension() != ".h")
                continue;
            m_shaderFiles.push_back(p.string());
            std::string rel = p.string();
            if (rel.size() > shaderDir.size())
                rel = rel.substr(shaderDir.size());
            std::replace(rel.begin(), rel.end(), '\\', '/');
            m_shaderRelPaths.push_back(rel);
        }
        m_shaderFilesScanned = true;
    }

    void ShaderEditor::LoadShaderFile(const std::string &path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open())
            return;
        std::string source((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        m_shaderOriginalSource = source;
        m_shaderModified = false;
        auto ext = std::filesystem::path(path).extension().string();
        if (ext == ".glsl")
            m_editor.SetLanguageDefinition(TextEditor::LanguageDefinition::GLSL());
        else
            m_editor.SetLanguageDefinition(TextEditor::LanguageDefinition::HLSL());
        m_editor.SetText(source);
    }

    void ShaderEditor::SaveAndRecompile()
    {
        if (m_selectedShader < 0 || m_selectedShader >= (int)m_shaderFiles.size())
            return;
        const std::string &path = m_shaderFiles[m_selectedShader];
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f.is_open())
        {
            PE_WARN("ShaderEditor: cannot write '%s'", path.c_str());
            return;
        }
        std::string src = m_editor.GetText();
        f << src;
        f.close();
        m_shaderOriginalSource = src;
        m_shaderModified = false;
        EventSystem::PushEvent(EventType::CompileShaders);
    }

    void ShaderEditor::Update()
    {
        if (!m_open)
            return;

        ImGui::SetNextWindowSize({900, 600}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(m_name.c_str(), &m_open))
        {
            ImGui::End();
            return;
        }

        if (!m_shaderFilesScanned)
        {
            ScanShaderFiles();
            if (!m_paletteInitialized)
            {
                m_paletteInitialized = true;
                m_editor.SetPalette(s_vscode);
                m_editor.SetShowWhitespaces(false);
            }
        }

        // Recompile button
        if (m_shaderModified)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.f));
            if (ImGui::Button("Save & Recompile", {-1, 0}))
                SaveAndRecompile();
            ImGui::PopStyleColor();
            ui::ItemTooltip("Save the edited shader file and recompile shaders.");
        }
        else
        {
            if (ImGui::Button("Recompile All Shaders", {-1, 0}))
                EventSystem::PushEvent(EventType::CompileShaders);
            ui::ItemTooltip("Recompile all discovered shader files.");
        }

        // Toolbar
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 270.f);
        ImGui::InputTextWithHint("##shsearch", "filter shaders...", m_shaderSearchFilter,
                                 IM_ARRAYSIZE(m_shaderSearchFilter));
        ui::ItemTooltip("Filter the shader file list by path.");
        ImGui::SameLine();
        static const char *paletteNames[] = {"Dark", "Light", "Retro", "Soft", "VSCode"};
        ImGui::SetNextItemWidth(100.f);
        if (ImGui::Combo("##palette", &m_editorPalette, paletteNames, 5))
        {
            if (m_editorPalette == 0)
                m_editor.SetPalette(TextEditor::GetDarkPalette());
            else if (m_editorPalette == 1)
                m_editor.SetPalette(TextEditor::GetLightPalette());
            else if (m_editorPalette == 2)
                m_editor.SetPalette(TextEditor::GetRetroBluePalette());
            else if (m_editorPalette == 3)
                m_editor.SetPalette(s_soft);
            else
                m_editor.SetPalette(s_vscode);
        }
        ui::ItemTooltip("Choose the source editor color palette.");
        ImGui::SameLine();
        static const char *sizeNames[] = {"S", "M", "L", "XL"};
        static const float sizeScales[] = {0.85f, 1.0f, 1.25f, 1.5f};
        ImGui::SetNextItemWidth(55.f);
        if (ImGui::Combo("##fontsize", &m_editorFontSizeIdx, sizeNames, 4))
            m_editorFontScale = sizeScales[m_editorFontSizeIdx];
        ui::ItemTooltip("Set the shader editor font size.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Rescan"))
        {
            m_shaderFilesScanned = false;
            m_selectedShader = -1;
            m_editor.SetText("");
            m_shaderModified = false;
        }
        ui::ItemTooltip("Rescan the shader directory and clear the current selection.");
        ImGui::Separator();

        float listW = 200.f;
        float editorW = ImGui::GetContentRegionAvail().x - listW - 8.f;
        float availH = ImGui::GetContentRegionAvail().y;

        // Left: file list
        ImGui::BeginChild("##shader_list", {listW, availH}, true);
        for (int i = 0; i < (int)m_shaderRelPaths.size(); ++i)
        {
            if (m_shaderSearchFilter[0] != '\0')
            {
                std::string_view hay(m_shaderRelPaths[i]), needle(m_shaderSearchFilter);
                bool found = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                                         [](unsigned char a, unsigned char b)
                                         { return std::tolower(a) == std::tolower(b); }) != hay.end();
                if (!found)
                    continue;
            }
            bool selected = (m_selectedShader == i);
            if (ImGui::Selectable(m_shaderRelPaths[i].c_str(), selected))
            {
                if (m_selectedShader != i)
                {
                    m_selectedShader = i;
                    LoadShaderFile(m_shaderFiles[i]);
                }
            }
            ui::ItemTooltip("Open this shader file in the editor.");
        }
        ImGui::EndChild();
        ImGui::SameLine();

        // Right: source editor
        ImGui::BeginChild("##shader_editor", {editorW, availH}, false);
        if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl)
        {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.f)
                m_editorFontScale = std::clamp(m_editorFontScale + wheel * 0.05f, 0.5f, 2.0f);
        }
        ImGui::SetWindowFontScale(m_editorFontScale);

        if (m_selectedShader >= 0)
        {
            m_editor.Render("##src", {-1, availH});
            if (m_editor.IsTextChanged())
                m_shaderModified = (m_editor.GetText() != m_shaderOriginalSource);
            if (m_shaderModified && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
                ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
                SaveAndRecompile();
        }
        else
        {
            ImGui::TextDisabled("Select a shader file to view and edit its source.");
        }
        ImGui::EndChild();
        ImGui::End();
    }
} // namespace pe
