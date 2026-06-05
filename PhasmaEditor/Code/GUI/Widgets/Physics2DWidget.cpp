#ifdef PE_PHYSICS2D

#include "Physics2DWidget.h"
#include "GUI/GUI.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Systems/Physics2DSystem.h"
#include "imgui/imgui.h"

namespace pe
{
    namespace
    {
        bool DragPositiveFloat(const char *label, float &value, float speed = 0.01f)
        {
            return ImGui::DragFloat(label, &value, speed, 0.001f, 1000.0f, "%.3f");
        }

        Physics2DBodyType ClampBodyType(Physics2DBodyType type)
        {
            int value = std::clamp(static_cast<int>(type), 0, 2);
            return static_cast<Physics2DBodyType>(value);
        }

        Physics2DShapeType ClampShapeType(Physics2DShapeType type)
        {
            int value = std::clamp(static_cast<int>(type), 0, 2);
            return static_cast<Physics2DShapeType>(value);
        }
    } // namespace

    void Physics2DWidget::DrawEmbed(NodeId *node, Scene *scene)
    {
        auto *physics = GetGlobalSystem<Physics2DSystem>();
        if (!physics || !scene || !node)
            return;

        Physics2DBodyDesc *desc = physics->GetBodyDesc(node);
        if (!desc)
            return;

        Physics2DBodyDesc edit = *desc;
        edit.bodyType = ClampBodyType(edit.bodyType);
        edit.shapeType = ClampShapeType(edit.shapeType);
        bool changed = false;
        bool rebuildNow = false;

        auto noteEdit = [&](bool edited, bool deferWhileActive = true)
        {
            if (edited)
            {
                changed = true;
                if (!deferWhileActive || !ImGui::IsItemActive())
                    rebuildNow = true;
            }
            if (deferWhileActive && ImGui::IsItemDeactivatedAfterEdit())
                rebuildNow = true;
        };

        ImGui::Text("Body ID: %u", physics->GetBodyId(node));

        static const char *bodyTypeNames[] = {"Static", "Kinematic", "Dynamic"};
        int bodyType = static_cast<int>(edit.bodyType);
        if (ImGui::Combo("Body Type", &bodyType, bodyTypeNames, IM_ARRAYSIZE(bodyTypeNames)))
        {
            edit.bodyType = static_cast<Physics2DBodyType>(bodyType);
            changed = true;
            rebuildNow = true;
        }

        static const char *shapeTypeNames[] = {"Box", "Circle", "Capsule"};
        int shapeType = static_cast<int>(edit.shapeType);
        if (ImGui::Combo("Shape Type", &shapeType, shapeTypeNames, IM_ARRAYSIZE(shapeTypeNames)))
        {
            edit.shapeType = static_cast<Physics2DShapeType>(shapeType);
            changed = true;
            rebuildNow = true;
        }

        switch (edit.shapeType)
        {
        case Physics2DShapeType::Box:
            noteEdit(DragPositiveFloat("Width", edit.width));
            noteEdit(DragPositiveFloat("Height", edit.height));
            break;
        case Physics2DShapeType::Circle:
            noteEdit(DragPositiveFloat("Radius", edit.radius));
            break;
        case Physics2DShapeType::Capsule:
            noteEdit(DragPositiveFloat("Capsule Height", edit.capsuleHeight));
            noteEdit(DragPositiveFloat("Capsule Radius", edit.capsuleRadius));
            break;
        }

        ImGui::SeparatorText("Material");
        noteEdit(ImGui::DragFloat("Density", &edit.density, 0.01f, 0.001f, 10000.0f, "%.3f"));
        noteEdit(ImGui::DragFloat("Friction", &edit.friction, 0.01f, 0.0f, 10.0f, "%.3f"));
        noteEdit(ImGui::DragFloat("Restitution", &edit.restitution, 0.01f, 0.0f, 10.0f, "%.3f"));
        noteEdit(ImGui::DragFloat("Linear Damping", &edit.linearDamping, 0.01f, 0.0f, 100.0f, "%.3f"));
        noteEdit(ImGui::DragFloat("Angular Damping", &edit.angularDamping, 0.01f, 0.0f, 100.0f, "%.3f"));
        noteEdit(ImGui::DragFloat("Gravity Scale", &edit.gravityScale, 0.01f, -100.0f, 100.0f, "%.3f"));

        ImGui::SeparatorText("Flags");
        noteEdit(ImGui::Checkbox("Sensor", &edit.isSensor), false);
        noteEdit(ImGui::Checkbox("Fixed Rotation", &edit.fixedRotation), false);
        noteEdit(ImGui::Checkbox("Bullet", &edit.bullet), false);
        noteEdit(ImGui::Checkbox("Enable Sleep", &edit.enableSleep), false);
        noteEdit(ImGui::Checkbox("Sync Node Transform", &edit.syncNode), false);

        ImGui::SeparatorText("Collision Filter");
        uint64_t categoryBits = edit.categoryBits;
        bool filterEdited = ImGui::InputScalar("Category Bits", ImGuiDataType_U64, &categoryBits);
        if (filterEdited)
        {
            edit.categoryBits = categoryBits;
        }
        noteEdit(filterEdited);
        uint64_t maskBits = edit.maskBits;
        filterEdited = ImGui::InputScalar("Mask Bits", ImGuiDataType_U64, &maskBits);
        if (filterEdited)
        {
            edit.maskBits = maskBits;
        }
        noteEdit(filterEdited);
        noteEdit(ImGui::InputInt("Group Index", &edit.groupIndex));

        ImGui::Spacing();
        if (ImGui::SmallButton("Remove Physics2D"))
        {
            physics->RemoveBody(node);
            if (m_gui)
                m_gui->NotifyChange();
            return;
        }

        if (!changed && !rebuildNow)
            return;

        edit.width = std::max(edit.width, 0.001f);
        edit.height = std::max(edit.height, 0.001f);
        edit.radius = std::max(edit.radius, 0.001f);
        edit.capsuleRadius = std::max(edit.capsuleRadius, 0.001f);
        edit.capsuleHeight = std::max(edit.capsuleHeight, edit.capsuleRadius * 2.0f);

        Physics2DBodyDesc rebuildDesc = changed ? edit : *desc;
        if (changed)
            *desc = rebuildDesc;

        if (rebuildNow)
            physics->AddBody(*scene, node, rebuildDesc);

        if (m_gui)
            m_gui->NotifyChange();
    }
} // namespace pe

#endif // PE_PHYSICS2D
