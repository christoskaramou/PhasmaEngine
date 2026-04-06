#include "GUI/Widgets/MaterialEditorWidget.h"
#include "Scene/Material.h"
#include "Scene/PassInfoAsset.h"
#include "Scene/SceneNode.h"
#include "Scene/Scene.h"
#include "API/Image.h"
#include <imgui.h>

namespace pe
{
    static MaterialParamValue MakeDefaultValue(const MaterialFieldDesc &field)
    {
        if (field.baseType == spirv_cross::SPIRType::Int)
            return int32_t(0);
        if (field.baseType == spirv_cross::SPIRType::UInt)
            return uint32_t(0);
        if (field.vecSize == 1)
            return 0.f;
        if (field.vecSize == 2)
            return vec2(0.f);
        if (field.vecSize == 3)
            return vec3(0.f);
        return vec4(0.f);
    }

    bool MaterialEditorWidget::Draw(Mesh *mesh, NodeId *node, Scene &scene)
    {
        if (!mesh || !mesh->material)
            return false;

        Material &mat = *mesh->material;
        MaterialInstance *inst = mesh->materialInstance;
        bool modified = false;

        // Show "Instance" vs "Base" label
        if (inst)
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.f, 1.f), "Editing: Instance");
        else
            ImGui::TextDisabled("Editing: Base Material");

        modified |= DrawPassInfoSelector(mat);

        if (!mat.passInfoAsset)
        {
            ImGui::TextDisabled("No PassInfo assigned - using legacy PBR");
            return modified;
        }

        // Refresh cached layout if PassInfo changed
        std::string passId = mat.passInfoAsset->GetResourceId();
        if (passId != m_cachedPassInfoId)
        {
            m_cachedLayout = ReflectMaterialLayout(*mat.passInfoAsset);
            m_cachedPassInfoId = passId;
            mat.cachedLayout = m_cachedLayout;
        }

        if (!m_cachedLayout.valid)
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "No pe_material bindings found in shader");
            return modified;
        }

        if (ImGui::TreeNodeEx("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (inst)
                modified |= DrawInstanceParams(*inst, m_cachedLayout);
            else
                modified |= DrawReflectedParams(mat, m_cachedLayout);
            ImGui::TreePop();
        }

        if (!m_cachedLayout.textureSlots.empty() && ImGui::TreeNodeEx("Textures", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (inst)
                modified |= DrawInstanceTextures(*inst, m_cachedLayout);
            else
                modified |= DrawReflectedTextures(mat, m_cachedLayout);
            ImGui::TreePop();
        }

        // Instance override management buttons
        if (inst && ImGui::TreeNodeEx("Overrides", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Button("Promote All to Base"))
            {
                for (const auto &field : m_cachedLayout.fields)
                {
                    if (inst->HasParamOverride(field.name))
                        mat.params[field.name] = inst->GetParam(field.name);
                }
                for (const auto &slot : m_cachedLayout.textureSlots)
                {
                    Image *texOverride = inst->GetNamedTexture(slot.bindingName);
                    if (texOverride)
                        mat.namedTextures[slot.bindingName] = ResourceManager::Get().Load<Image>(texOverride->GetResourceId());
                }
                mat.dirty = true;
                modified = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear All Overrides"))
            {
                for (const auto &field : m_cachedLayout.fields)
                    inst->ClearParam(field.name);
                for (const auto &slot : m_cachedLayout.textureSlots)
                    inst->ClearNamedTexture(slot.bindingName);
                inst->dirty = true;
                modified = true;
            }
            ImGui::TreePop();
        }

        if (modified)
        {
            if (inst)
                inst->dirty = true;
            else
                mat.dirty = true;
        }

        return modified;
    }

    bool MaterialEditorWidget::DrawPassInfoSelector(Material &mat)
    {
        bool changed = false;
        std::string currentName = mat.passInfoAsset ? mat.passInfoAsset->GetName() : "(none)";

        ImGui::Text("PassInfo:");
        ImGui::SameLine();
        if (ImGui::Button(currentName.c_str(), ImVec2(-1, 0)))
        {
            // TODO: open file picker for .pass files
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
            {
                std::string path(static_cast<const char *>(payload->Data), payload->DataSize - 1);
                if (path.ends_with(".pass"))
                {
                    mat.passInfoAsset = ResourceManager::Get().Load<PassInfoAsset>(path);
                    changed = true;
                }
            }
            ImGui::EndDragDropTarget();
        }

        return changed;
    }

    bool MaterialEditorWidget::DrawReflectedParams(Material &mat, const MaterialLayout &layout)
    {
        bool modified = false;

        for (const auto &field : layout.fields)
        {
            auto it = mat.params.find(field.name);
            MaterialParamValue value = (it != mat.params.end()) ? it->second : MakeDefaultValue(field);

            if (DrawField(field, value))
            {
                mat.params[field.name] = value;
                modified = true;
            }
        }

        return modified;
    }

    bool MaterialEditorWidget::DrawInstanceParams(MaterialInstance &inst, const MaterialLayout &layout)
    {
        bool modified = false;
        Material *parent = inst.GetParent();

        for (const auto &field : layout.fields)
        {
            bool hasOverride = inst.HasParamOverride(field.name);

            // Get effective value: override or parent fallback
            MaterialParamValue value;
            if (hasOverride)
            {
                value = inst.GetParam(field.name);
            }
            else
            {
                auto it = parent->params.find(field.name);
                value = (it != parent->params.end()) ? it->second : MakeDefaultValue(field);
            }

            // Dim non-overridden fields
            if (!hasOverride)
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.6f);

            if (DrawField(field, value))
            {
                inst.SetParam(field.name, value);
                modified = true;
            }

            if (!hasOverride)
                ImGui::PopStyleVar();

            // Right-click context menu for per-field override management
            if (ImGui::BeginPopupContextItem(field.name.c_str()))
            {
                if (hasOverride)
                {
                    if (ImGui::MenuItem("Clear Override"))
                    {
                        inst.ClearParam(field.name);
                        modified = true;
                    }
                    if (ImGui::MenuItem("Promote to Base"))
                    {
                        parent->params[field.name] = inst.GetParam(field.name);
                        inst.ClearParam(field.name);
                        parent->dirty = true;
                        modified = true;
                    }
                }
                else
                {
                    ImGui::TextDisabled("No override (using base)");
                }
                ImGui::EndPopup();
            }
        }

        return modified;
    }

    bool MaterialEditorWidget::DrawField(const MaterialFieldDesc &field, MaterialParamValue &value)
    {
        ImGui::PushID(field.name.c_str());
        bool changed = false;

        switch (field.hint)
        {
        case MaterialWidgetHint::Color4:
        {
            vec4 &v = std::get<vec4>(value);
            changed = ImGui::ColorEdit4(field.name.c_str(), &v.x);
            break;
        }
        case MaterialWidgetHint::Color3:
        {
            vec3 &v = std::get<vec3>(value);
            changed = ImGui::ColorEdit3(field.name.c_str(), &v.x);
            break;
        }
        case MaterialWidgetHint::Range01:
        {
            float &v = std::get<float>(value);
            changed = ImGui::SliderFloat(field.name.c_str(), &v, 0.f, 1.f);
            break;
        }
        case MaterialWidgetHint::Range:
        {
            float &v = std::get<float>(value);
            changed = ImGui::SliderFloat(field.name.c_str(), &v, field.rangeMin, field.rangeMax);
            break;
        }
        case MaterialWidgetHint::Auto:
        default:
        {
            std::visit([&](auto &v)
                       {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, float>)
                    changed = ImGui::DragFloat(field.name.c_str(), &v, 0.01f);
                else if constexpr (std::is_same_v<T, vec2>)
                    changed = ImGui::DragFloat2(field.name.c_str(), &v.x, 0.01f);
                else if constexpr (std::is_same_v<T, vec3>)
                    changed = ImGui::DragFloat3(field.name.c_str(), &v.x, 0.01f);
                else if constexpr (std::is_same_v<T, vec4>)
                    changed = ImGui::ColorEdit4(field.name.c_str(), &v.x);
                else if constexpr (std::is_same_v<T, int32_t>)
                    changed = ImGui::DragInt(field.name.c_str(), &v);
                else if constexpr (std::is_same_v<T, uint32_t>)
                {
                    int tmp = static_cast<int>(v);
                    if (ImGui::DragInt(field.name.c_str(), &tmp, 1.f, 0, INT32_MAX))
                    {
                        v = static_cast<uint32_t>(tmp);
                        changed = true;
                    }
                } },
                       value);
            break;
        }
        }

        ImGui::PopID();
        return changed;
    }

    bool MaterialEditorWidget::DrawReflectedTextures(Material &mat, const MaterialLayout &layout)
    {
        bool modified = false;

        for (const auto &slot : layout.textureSlots)
        {
            auto it = mat.namedTextures.find(slot.bindingName);
            std::string label = slot.bindingName;

            // Strip pe_tex_ prefix for display
            if (label.starts_with("pe_tex_"))
                label = label.substr(7);

            Image *current = (it != mat.namedTextures.end() && it->second) ? it->second.get() : nullptr;

            ImGui::Text("%s:", label.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled(current ? current->GetResourceId().c_str() : "(none)");

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                {
                    std::string path(static_cast<const char *>(payload->Data), payload->DataSize - 1);
                    mat.namedTextures[slot.bindingName] = ResourceManager::Get().Load<Image>(path);
                    modified = true;
                }
                ImGui::EndDragDropTarget();
            }
        }

        return modified;
    }

    bool MaterialEditorWidget::DrawInstanceTextures(MaterialInstance &inst, const MaterialLayout &layout)
    {
        bool modified = false;
        Material *parent = inst.GetParent();

        for (const auto &slot : layout.textureSlots)
        {
            std::string label = slot.bindingName;
            if (label.starts_with("pe_tex_"))
                label = label.substr(7);

            Image *overrideTex = inst.GetNamedTexture(slot.bindingName);
            Image *parentTex = nullptr;
            {
                auto it = parent->namedTextures.find(slot.bindingName);
                if (it != parent->namedTextures.end())
                    parentTex = it->second.get();
            }

            Image *effective = overrideTex ? overrideTex : parentTex;
            bool hasOverride = (overrideTex != nullptr);

            if (!hasOverride)
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.6f);

            ImGui::Text("%s:", label.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled(effective ? effective->GetResourceId().c_str() : "(none)");

            if (!hasOverride)
                ImGui::PopStyleVar();

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                {
                    std::string path(static_cast<const char *>(payload->Data), payload->DataSize - 1);
                    inst.SetNamedTexture(slot.bindingName, ResourceManager::Get().Load<Image>(path));
                    modified = true;
                }
                ImGui::EndDragDropTarget();
            }

            // Right-click context menu
            if (ImGui::BeginPopupContextItem(slot.bindingName.c_str()))
            {
                if (hasOverride)
                {
                    if (ImGui::MenuItem("Clear Override"))
                    {
                        inst.ClearNamedTexture(slot.bindingName);
                        modified = true;
                    }
                    if (ImGui::MenuItem("Promote to Base"))
                    {
                        parent->namedTextures[slot.bindingName] = ResourceManager::Get().Load<Image>(overrideTex->GetResourceId());
                        inst.ClearNamedTexture(slot.bindingName);
                        parent->dirty = true;
                        modified = true;
                    }
                }
                else
                {
                    ImGui::TextDisabled("No override (using base)");
                }
                ImGui::EndPopup();
            }
        }

        return modified;
    }
} // namespace pe
