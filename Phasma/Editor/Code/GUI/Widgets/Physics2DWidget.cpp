#ifdef PE_PHYSICS2D

#include "Physics2DWidget.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
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
        ui::ItemTooltip("Select whether the 2D body is static, kinematic, or dynamically simulated.");

        static const char *shapeTypeNames[] = {"Box", "Circle", "Capsule"};
        int shapeType = static_cast<int>(edit.shapeType);
        if (ImGui::Combo("Shape Type", &shapeType, shapeTypeNames, IM_ARRAYSIZE(shapeTypeNames)))
        {
            edit.shapeType = static_cast<Physics2DShapeType>(shapeType);
            changed = true;
            rebuildNow = true;
        }
        ui::ItemTooltip("Choose the 2D collision shape for this body.");

        switch (edit.shapeType)
        {
        case Physics2DShapeType::Box:
            noteEdit(DragPositiveFloat("Width", edit.width));
            ui::ItemTooltip("Width of the box collider in scene units.");
            noteEdit(DragPositiveFloat("Height", edit.height));
            ui::ItemTooltip("Height of the box collider in scene units.");
            break;
        case Physics2DShapeType::Circle:
            noteEdit(DragPositiveFloat("Radius", edit.radius));
            ui::ItemTooltip("Radius of the circle collider.");
            break;
        case Physics2DShapeType::Capsule:
            noteEdit(DragPositiveFloat("Capsule Height", edit.capsuleHeight));
            ui::ItemTooltip("Overall height of the capsule collider.");
            noteEdit(DragPositiveFloat("Capsule Radius", edit.capsuleRadius));
            ui::ItemTooltip("Radius of the capsule ends.");
            break;
        }

        ImGui::SeparatorText("Material");
        noteEdit(ImGui::DragFloat("Density", &edit.density, 0.01f, 0.001f, 10000.0f, "%.3f"));
        ui::ItemTooltip("Mass density used when Box2D computes body mass.");
        noteEdit(ImGui::DragFloat("Friction", &edit.friction, 0.01f, 0.0f, 10.0f, "%.3f"));
        ui::ItemTooltip("Surface resistance when this body contacts another.");
        noteEdit(ImGui::DragFloat("Restitution", &edit.restitution, 0.01f, 0.0f, 10.0f, "%.3f"));
        ui::ItemTooltip("Bounciness applied during 2D contacts.");
        noteEdit(ImGui::DragFloat("Linear Damping", &edit.linearDamping, 0.01f, 0.0f, 100.0f, "%.3f"));
        ui::ItemTooltip("Slows translational velocity over time.");
        noteEdit(ImGui::DragFloat("Angular Damping", &edit.angularDamping, 0.01f, 0.0f, 100.0f, "%.3f"));
        ui::ItemTooltip("Slows rotational velocity over time.");
        noteEdit(ImGui::DragFloat("Gravity Scale", &edit.gravityScale, 0.01f, -100.0f, 100.0f, "%.3f"));
        ui::ItemTooltip("Multiplier for the world's gravity on this body.");

        ImGui::SeparatorText("Flags");
        noteEdit(ImGui::Checkbox("Sensor", &edit.isSensor), false);
        ui::ItemTooltip("Detect overlaps without producing collision response.");
        noteEdit(ImGui::Checkbox("Fixed Rotation", &edit.fixedRotation), false);
        ui::ItemTooltip("Prevent physics contacts from rotating this body.");
        noteEdit(ImGui::Checkbox("Bullet", &edit.bullet), false);
        ui::ItemTooltip("Use continuous collision detection for fast-moving bodies.");
        noteEdit(ImGui::Checkbox("Enable Sleep", &edit.enableSleep), false);
        ui::ItemTooltip("Allow Box2D to sleep this body when it is inactive.");
        noteEdit(ImGui::Checkbox("Sync Node Transform", &edit.syncNode), false);
        ui::ItemTooltip("Write the simulated 2D body transform back to the scene node.");

        ImGui::SeparatorText("Collision Filter");
        uint64_t categoryBits = edit.categoryBits;
        bool filterEdited = ImGui::InputScalar("Category Bits", ImGuiDataType_U64, &categoryBits);
        if (filterEdited)
        {
            edit.categoryBits = categoryBits;
        }
        noteEdit(filterEdited);
        ui::ItemTooltip("Bitfield describing which collision categories this body belongs to.");
        uint64_t maskBits = edit.maskBits;
        filterEdited = ImGui::InputScalar("Mask Bits", ImGuiDataType_U64, &maskBits);
        if (filterEdited)
        {
            edit.maskBits = maskBits;
        }
        noteEdit(filterEdited);
        ui::ItemTooltip("Bitfield describing which categories this body can collide with.");
        noteEdit(ImGui::InputInt("Group Index", &edit.groupIndex));
        ui::ItemTooltip("Optional Box2D group override for always-collide or never-collide pairs.");

        ImGui::Spacing();
        if (ImGui::SmallButton("Remove Physics2D"))
        {
            physics->RemoveBody(node);
            if (m_gui)
                m_gui->NotifyChange();
            return;
        }
        ui::ItemTooltip("Remove the 2D physics body from this node.");

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
