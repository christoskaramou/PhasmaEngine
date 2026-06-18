#include "SceneScripts.h"
#include "GUI/Helpers.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include <cstring>

namespace pe
{
    // Immediate-mode text field bound to a std::string: copy out, edit, copy back if changed.
    static void EditString(const char *id, std::string &value, float width, bool &dirty)
    {
        char buf[512];
        std::strncpy(buf, value.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        if (width > 0.0f)
            ImGui::SetNextItemWidth(width);
        if (ImGui::InputText(id, buf, sizeof(buf)))
        {
            value = buf;
            dirty = true;
        }
    }

    SceneScripts::SceneScripts() : Widget("Scene Scripts")
    {
        m_open = false; // hidden until toggled from the window menu
    }

    void SceneScripts::Update()
    {
        if (!m_open)
            return;

        ui::SetInitialWindowSizeFraction(1.0f / 5.0f, 0.4f, ImGuiCond_Appearing);
        if (!ImGui::Begin("Scene Scripts", &m_open))
        {
            ImGui::End();
            return;
        }

        Scene *scene = GetActiveScene();
        if (!scene)
        {
            ImGui::TextDisabled("No active scene.");
            ImGui::End();
            return;
        }

        SceneScriptManifest &manifest = scene->GetScriptManifest();
        bool dirty = false;

        ImGui::TextWrapped("Scene-owned Lua scripts, stored in the .pescene. Paths are project-relative "
                           "(e.g. Scripts/Scenes/foo.lua). Changes persist on Save Scene.");
        ImGui::Separator();

        // --- On Play scripts ------------------------------------------------------------------
        if (ImGui::CollapsingHeader("On Play", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("Run with PlayerOnly lifecycle when the scene enters play.");
            int removeIdx = -1;
            for (int i = 0; i < static_cast<int>(manifest.onPlay.size()); i++)
            {
                ImGui::PushID(i);
                const float w = ImGui::GetContentRegionAvail().x - 30.0f;
                EditString("##onplay", manifest.onPlay[i], w, dirty);
                ImGui::SameLine();
                if (ImGui::SmallButton("X"))
                    removeIdx = i;
                ImGui::PopID();
            }
            if (removeIdx >= 0)
            {
                manifest.onPlay.erase(manifest.onPlay.begin() + removeIdx);
                dirty = true;
            }
            if (ImGui::Button("+ Add Script"))
            {
                manifest.onPlay.emplace_back("Scripts/");
                dirty = true;
            }
        }

        // --- Actions --------------------------------------------------------------------------
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Actions", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("Named entry points invoked via scene.run_action(\"id\").");
            if (!manifest.actions.empty())
            {
                ImGui::Columns(3, "##scene_action_cols", false);
                ImGui::TextDisabled("Id");
                ImGui::NextColumn();
                ImGui::TextDisabled("Script");
                ImGui::NextColumn();
                ImGui::TextDisabled("Function");
                ImGui::NextColumn();
                ImGui::Columns(1);
            }
            int removeIdx = -1;
            for (int i = 0; i < static_cast<int>(manifest.actions.size()); i++)
            {
                ImGui::PushID(1000 + i);
                SceneScriptAction &a = manifest.actions[i];
                EditString("##aid", a.id, 130.0f, dirty);
                ImGui::SameLine();
                EditString("##ascript", a.script, 220.0f, dirty);
                ImGui::SameLine();
                EditString("##afn", a.function, 130.0f, dirty);
                ImGui::SameLine();
                if (ImGui::SmallButton("X"))
                    removeIdx = i;
                ImGui::PopID();
            }
            if (removeIdx >= 0)
            {
                manifest.actions.erase(manifest.actions.begin() + removeIdx);
                dirty = true;
            }
            if (ImGui::Button("+ Add Action"))
            {
                SceneScriptAction a;
                a.id = "action";
                a.script = "Scripts/";
                a.function = "func";
                manifest.actions.push_back(std::move(a));
                dirty = true;
            }
        }

        if (dirty)
            scene->MarkDirty();

        ImGui::End();
    }
} // namespace pe
