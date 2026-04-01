#ifdef PE_PHYSICS

#include "PhysicsWidget.h"
#include "imgui/imgui.h"
#include "Physics/PhysicsTypes.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Systems/PhysicsSystem.h"
#include "Systems/RendererSystem.h"

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

        static const char *bodyTypeNames[] = {"Static", "Dynamic", "Kinematic"};
        static const char *shapeTypeNames[] = {"Box", "Sphere", "Capsule", "Convex Hull"};

        int bodyType = static_cast<int>(desc->bodyType);
        if (ImGui::Combo("Body Type", &bodyType, bodyTypeNames, IM_ARRAYSIZE(bodyTypeNames)))
            desc->bodyType = static_cast<PhysicsBodyType>(bodyType);

        int shapeType = static_cast<int>(desc->shapeType);
        if (ImGui::Combo("Shape Type", &shapeType, shapeTypeNames, IM_ARRAYSIZE(shapeTypeNames)))
            desc->shapeType = static_cast<PhysicsShapeType>(shapeType);

        ImGui::Checkbox("Auto Fit Shape", &desc->autoFitShape);

        if (!desc->autoFitShape)
        {
            switch (desc->shapeType)
            {
            case PhysicsShapeType::Box:
                ImGui::DragFloat3("Half Extents", &desc->boxHalfExtents.x, 0.01f, 0.001f, 100.0f);
                break;
            case PhysicsShapeType::Sphere:
                ImGui::DragFloat("Radius", &desc->sphereRadius, 0.01f, 0.001f, 100.0f);
                break;
            case PhysicsShapeType::Capsule:
                ImGui::DragFloat("Half Height", &desc->capsuleHalfHeight, 0.01f, 0.001f, 100.0f);
                ImGui::DragFloat("Capsule Radius", &desc->capsuleRadius, 0.01f, 0.001f, 100.0f);
                break;
            case PhysicsShapeType::ConvexHull:
                ImGui::TextDisabled("Generated from mesh geometry");
                break;
            }
        }

        if (desc->bodyType != PhysicsBodyType::Static)
            ImGui::DragFloat("Mass", &desc->mass, 0.1f, 0.001f, 10000.0f);

        ImGui::DragFloat("Friction", &desc->friction, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Restitution", &desc->restitution, 0.01f, 0.0f, 1.0f);

        ImGui::Spacing();
        if (ImGui::SmallButton("Remove Physics"))
            ps->RemoveBody(node);
    }
} // namespace pe

#endif // PE_PHYSICS
