#include "GUI/Widgets/MaterialEditorWidget.h"
#include "Scene/Material.h"
#include "Scene/PassInfoAsset.h"
#include "Scene/SceneNode.h"
#include "Scene/Scene.h"
#include "API/Image.h"
#include <imgui.h>

namespace pe
{
    bool MaterialEditorWidget::Draw(Mesh *mesh, NodeId *node, Scene &scene)
    {
        if (!mesh || !mesh->material)
            return false;

        Material &mat = *mesh->material;
        bool modified = false;

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
        }

        if (!m_cachedLayout.valid)
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "No pe_material bindings found in shader");
            return modified;
        }

        if (ImGui::TreeNodeEx("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
        {
            modified |= DrawReflectedParams(mat, m_cachedLayout);
            ImGui::TreePop();
        }

        if (!m_cachedLayout.textureSlots.empty() && ImGui::TreeNodeEx("Textures", ImGuiTreeNodeFlags_DefaultOpen))
        {
            modified |= DrawReflectedTextures(mat, m_cachedLayout);
            ImGui::TreePop();
        }

        if (modified)
            mat.dirty = true;

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
            MaterialParamValue value;

            if (it != mat.params.end())
            {
                value = it->second;
            }
            else
            {
                // Create default based on type
                if (field.baseType == spirv_cross::SPIRType::Int)
                    value = int32_t(0);
                else if (field.baseType == spirv_cross::SPIRType::UInt)
                    value = uint32_t(0);
                else if (field.vecSize == 1)
                    value = 0.f;
                else if (field.vecSize == 2)
                    value = vec2(0.f);
                else if (field.vecSize == 3)
                    value = vec3(0.f);
                else
                    value = vec4(0.f);
            }

            if (DrawField(field, value))
            {
                mat.params[field.name] = value;
                modified = true;
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
} // namespace pe
