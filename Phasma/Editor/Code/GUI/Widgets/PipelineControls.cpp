#include "PipelineControls.h"
#include "API/RHI.h"
#include "GUI/Helpers.h"
#include "RenderPasses/LightPass.h"
#include "RenderPasses/RayTracingPass.h"

namespace pe
{
    namespace
    {
        // Shadows and Forward+ change the lighting passes' bindings.
        void UpdateLightingDescriptorSets()
        {
            if (auto *pass = GetGlobalComponent<LightOpaquePass>())
                pass->UpdateDescriptorSets();
            if (auto *pass = GetGlobalComponent<LightTransparentPass>())
                pass->UpdateDescriptorSets();
            if (auto *pass = GetGlobalComponent<RayTracingPass>())
                pass->UpdateDescriptorSets();
        }

        std::vector<std::string> FindPipelineScripts()
        {
            std::vector<std::string> paths;
            std::error_code ec;
            const std::filesystem::path root(Path::Assets);
            for (auto it = std::filesystem::recursive_directory_iterator(root / "Scripts" / "Pipeline", std::filesystem::directory_options::skip_permission_denied, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
            {
                if (it->path().extension() == ".lua")
                    paths.push_back(it->path().lexically_relative(root).generic_string());
            }
            std::sort(paths.begin(), paths.end());
            return paths;
        }

        bool DrawPipelineScriptPicker(ScriptSystem &ss)
        {
            auto &gs = Settings::Get<SceneSettings>();
            static std::vector<std::string> s_scripts;
            constexpr const char *kDefault = "Engine default";
            bool changed = false;
            if (ImGui::BeginCombo(ui::LabelAbove("Pipeline Script"), gs.pipeline_script.empty() ? kDefault : gs.pipeline_script.c_str()))
            {
                if (ImGui::IsWindowAppearing())
                {
                    s_scripts = FindPipelineScripts();
                    for (const std::string &name : ss.ListCppPipelineScripts())
                        s_scripts.push_back("cpp:" + name);
                }
                std::string picked = gs.pipeline_script;
                if (ImGui::Selectable(kDefault, gs.pipeline_script.empty()))
                    picked.clear();
                ui::ItemTooltip(ScriptSystem::kDefaultPipelineScript);
                for (const std::string &path : s_scripts)
                    if (ImGui::Selectable(path.c_str(), path == gs.pipeline_script))
                        picked = path;
                ImGui::EndCombo();
                if (picked != gs.pipeline_script)
                {
                    gs.pipeline_script = picked;
                    ss.ReloadPipelineScript();
                    UpdateLightingDescriptorSets();
                    changed = true;
                }
            }
            ui::ItemTooltip("The script that sets up this scene's passes: a .lua under Scripts/Pipeline, or a C++ pipeline script "
                            "(cpp:) from the game module. Picking one applies its values.");
            return changed;
        }
    } // namespace

    bool DrawExposedValues(const std::vector<ExposedVar> &vars, lua_State *L,
                           const std::function<sol::object(const ExposedVar &)> &get,
                           const std::function<void(const ExposedVar &, const sol::object &)> &set, const char *tooltip)
    {
        // A value nests under the bool value its exposed_when names; anything else is a root.
        std::unordered_map<std::string, std::vector<const ExposedVar *>> children;
        std::vector<const ExposedVar *> roots;
        for (const ExposedVar &var : vars)
        {
            const bool nested = !var.shownWhen.empty() && std::any_of(vars.begin(), vars.end(), [&](const ExposedVar &v)
                                                                      { return v.name == var.shownWhen && v.type == ExposedVar::Type::Bool; });
            (nested ? children[var.shownWhen] : roots).push_back(&var);
        }
        bool changed = false;
        std::function<void(const ExposedVar &)> draw = [&](const ExposedVar &var)
        {
            const sol::object value = get(var);
            const std::string label = ui::ExposedLabel(var.name);
            sol::object edited;
            bool open = false;
            ImGui::PushID(var.name.c_str());
            switch (var.type)
            {
            case ExposedVar::Type::Bool:
            {
                bool flag = value.is<bool>() && value.as<bool>();
                if (ImGui::Checkbox(label.c_str(), &flag))
                    edited = sol::make_object(L, flag);
                open = flag;
                break;
            }
            case ExposedVar::Type::Number:
            {
                double number = value.is<double>() ? value.as<double>() : 0.0;
                const float speed = static_cast<float>(0.01 * std::max(1.0, std::abs(number)));
                if (ImGui::DragScalar(ui::LabelAbove(label.c_str()), ImGuiDataType_Double, &number, speed, nullptr, nullptr, "%.4g"))
                    edited = sol::make_object(L, number);
                break;
            }
            case ExposedVar::Type::String:
            {
                char buffer[256];
                std::snprintf(buffer, sizeof(buffer), "%s", value.is<std::string>() ? value.as<std::string>().c_str() : "");
                if (ImGui::InputText(ui::LabelAbove(label.c_str()), buffer, sizeof(buffer)))
                    edited = sol::make_object(L, std::string(buffer));
                break;
            }
            }
            if (tooltip)
                ui::ItemTooltip(tooltip);
            ImGui::PopID();
            if (edited.valid())
            {
                set(var, edited);
                changed = true;
            }
            if (const auto it = children.find(var.name); open && it != children.end())
            {
                ImGui::Indent(16.0f);
                for (const ExposedVar *child : it->second)
                    draw(*child);
                ImGui::Unindent(16.0f);
            }
        };
        for (const ExposedVar *root : roots)
            draw(*root);
        return changed;
    }

    bool DrawPipelineRenderPathControls()
    {
        auto &gs = Settings::Get<SceneSettings>();
        bool changed = false;
        {
            const char *rtModeNames[] = {"Raster", "Hybrid", "Ray Tracing"};
            const bool rtSupported = RHII.GetCaps().rayTracing;
            int currentMode = static_cast<int>(ClampRenderModeToRayTracingSupport(gs.render_mode, rtSupported));
            const int modeCount = rtSupported ? 3 : 1;
            if (ImGui::Combo(ui::LabelAbove("Render Mode"), &currentMode, rtModeNames, modeCount) &&
                currentMode != static_cast<int>(gs.render_mode))
            {
                changed = true;
                EventSystem::PushEvent(EventType::SetRenderMode, static_cast<RenderMode>(currentMode));
            }
            ui::ItemTooltip("Switch between raster, hybrid, and full ray-tracing render paths.");
        }
        changed |= ImGui::Checkbox("HDR Scene Color", &gs.hdr);
        ui::ItemTooltip("Preserve bright lighting for tone mapping and scene bloom before the HUD. Uses more GPU memory; GI enables this automatically.");
        bool dynamicRendering = gs.dynamic_rendering;
        ImGui::BeginDisabled(!RHII.GetCaps().dynamicRendering);
        if (ImGui::Checkbox("Dynamic Rendering", &dynamicRendering))
        {
            changed = true;
            EventSystem::PushEvent(EventType::DynamicRendering, dynamicRendering);
        }
        ImGui::EndDisabled();
        ui::ItemTooltip("Toggle Vulkan dynamic rendering path when supported.");
        return changed;
    }

    bool DrawPipelineControls()
    {
        bool changed = DrawPipelineRenderPathControls();
        if (!HasGlobalSystem<ScriptSystem>()) // the Animator runs no scripts
            return changed;
        ScriptSystem *ss = GetGlobalSystem<ScriptSystem>();
        changed |= DrawPipelineScriptPicker(*ss);

        const std::vector<ExposedVar> *values = ss->GetPipelineValues();
        if (!values)
        {
            ImGui::TextDisabled("The pipeline script did not load; the console has the error.");
            return changed;
        }
        ImGui::SeparatorText("Values");
        changed |= DrawExposedValues(
            *values, ss->GetLua().lua_state(), [ss](const ExposedVar &var)
            { return ss->GetPipelineValue(var); },
            [ss](const ExposedVar &var, const sol::object &value)
            {
                ss->SetPipelineValue(var, value);
                if (var.name == "shadows" || var.name == "forward_plus")
                    UpdateLightingDescriptorSets();
            });
        return changed;
    }
} // namespace pe
