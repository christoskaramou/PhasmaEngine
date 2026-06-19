#include "RuntimeUiPalette.h"
#include "GUI/Helpers.h"
#include "GUI/RuntimeUiAuthoring.h"
#include "imgui/imgui.h"

namespace pe
{
    void RuntimeUiPalette::Update()
    {
        if (!m_open)
            return;

        ImGui::Begin(m_name.c_str(), &m_open);

        for (const RuntimeUiAuthoring::ElementTemplate &element : RuntimeUiAuthoring::Templates())
        {
            ImGui::PushID(static_cast<int>(element.type));
            const std::string label = std::string(element.icon) + "  " + element.name;
            ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAvailWidth, ImVec2(0.0f, 34.0f));

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                RuntimeUiAuthoring::DragPayload payload{};
                payload.type = element.type;
                ImGui::SetDragDropPayload(RuntimeUiAuthoring::kDragPayloadType, &payload, sizeof(payload));
                ImGui::Text("%s  %s", element.icon, element.name);
                ImGui::EndDragDropSource();
            }
            ui::ItemTooltip(element.tooltip);
            ImGui::PopID();
        }

        ImGui::End();
    }
} // namespace pe
