#ifdef PE_PHYSICS

#include "PhysicsWidget.h"
#include "GUI/Helpers.h"
#include "Physics/PhysicsTypes.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Systems/PhysicsSystem.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui.h"

namespace pe
{
    void PhysicsWidget::DrawEmbed(NodeId *node, Scene *scene)
    {
        auto *ps = GetGlobalSystem<PhysicsSystem>();
        if (!ps)
            return;

        PhysicsBodyDesc *desc = ps->GetBodyDesc(node);
        if (!desc)
            return;

        // The terrain collider (Mesh shape) is cooked and owned by the Terrain node's "Physics Collision"
        // toggle. Show it read-only here — it's visible but not hand-editable, since the runtime host is
        // rebuilt on every terrain change and would overwrite any edit.
        if (desc->shapeType == PhysicsShapeType::Mesh)
        {
            ImGui::TextDisabled("Body Type:    Static");
            ImGui::TextDisabled("Shape:        Mesh (terrain — auto)");
            ImGui::TextDisabled("Friction:     %.2f", desc->friction);
            ImGui::TextDisabled("Restitution:  %.2f", desc->restitution);
            ImGui::Spacing();
            ImGui::TextWrapped("Managed by the Terrain node's Physics Collision toggle — "
                               "select that node to change friction / restitution.");
            return;
        }

        static const char *bodyTypeNames[] = {"Static", "Dynamic", "Kinematic"};
        static const char *shapeTypeNames[] = {"Box", "Sphere", "Capsule", "Convex Hull"};

        int bodyType = static_cast<int>(desc->bodyType);
        if (ImGui::Combo("Body Type", &bodyType, bodyTypeNames, IM_ARRAYSIZE(bodyTypeNames)))
            desc->bodyType = static_cast<PhysicsBodyType>(bodyType);
        ui::ItemTooltip("Select whether the body is static, simulated dynamically, or moved kinematically.");

        ImGui::Checkbox("Is Trigger", &desc->isTrigger);
        ui::ItemTooltip("Report overlaps without applying collision response.");

        bool shapeChanged = false;
        int shapeType = static_cast<int>(desc->shapeType);
        if (ImGui::Combo("Shape Type", &shapeType, shapeTypeNames, IM_ARRAYSIZE(shapeTypeNames)))
        {
            desc->shapeType = static_cast<PhysicsShapeType>(shapeType);
            shapeChanged = true;
        }
        ui::ItemTooltip("Choose the collision volume used by the physics body.");

        shapeChanged |= ImGui::Checkbox("Auto Fit Shape", &desc->autoFitShape);
        ui::ItemTooltip("Fit the collision shape from the selected node's render bounds.");

        if (!desc->autoFitShape)
        {
            switch (desc->shapeType)
            {
            case PhysicsShapeType::Box:
                shapeChanged |= ImGui::DragFloat3("Half Extents", &desc->boxHalfExtents.x, 0.01f, 0.001f, 100.0f);
                ui::ItemTooltip("Half-size of the box collider on X, Y, and Z.");
                break;
            case PhysicsShapeType::Sphere:
                shapeChanged |= ImGui::DragFloat("Radius", &desc->sphereRadius, 0.01f, 0.001f, 100.0f);
                ui::ItemTooltip("Radius of the sphere collider.");
                break;
            case PhysicsShapeType::Capsule:
                shapeChanged |= ImGui::DragFloat("Half Height", &desc->capsuleHalfHeight, 0.01f, 0.001f, 100.0f);
                ui::ItemTooltip("Half the straight section height of the capsule collider.");
                shapeChanged |= ImGui::DragFloat("Capsule Radius", &desc->capsuleRadius, 0.01f, 0.001f, 100.0f);
                ui::ItemTooltip("Radius of the rounded capsule ends.");
                break;
            case PhysicsShapeType::ConvexHull:
                ImGui::TextDisabled("Generated from mesh geometry");
                break;
            }
        }

        if (shapeChanged)
            ps->InvalidateShapeCache(node);

        if (desc->bodyType != PhysicsBodyType::Static)
            ImGui::DragFloat("Mass", &desc->mass, 0.1f, 0.001f, 10000.0f);
        if (desc->bodyType != PhysicsBodyType::Static)
            ui::ItemTooltip("Mass used by dynamic and kinematic body simulation.");

        ImGui::DragFloat("Friction", &desc->friction, 0.01f, 0.0f, 1.0f);
        ui::ItemTooltip("Surface resistance applied when this body contacts another.");
        ImGui::DragFloat("Restitution", &desc->restitution, 0.01f, 0.0f, 1.0f);
        ui::ItemTooltip("Bounciness applied during collisions.");
    }
} // namespace pe

#endif // PE_PHYSICS
