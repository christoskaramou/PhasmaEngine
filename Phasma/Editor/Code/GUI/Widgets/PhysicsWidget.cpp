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
        if (ImGui::Combo(ui::LabelAbove("Body Type"), &bodyType, bodyTypeNames, IM_ARRAYSIZE(bodyTypeNames)))
            desc->bodyType = static_cast<PhysicsBodyType>(bodyType);
        ui::ItemTooltip("Select whether the body is static, simulated dynamically, or moved kinematically.");

        ImGui::Checkbox("Is Trigger", &desc->isTrigger);
        ui::ItemTooltip("Report overlaps without applying collision response.");

        // Named layers come from Scene Settings > Physics Layers; an unnamed current layer still shows its index.
        const auto &layerNames = Settings::Get<SceneSettings>().physics_layer_names;
        auto layerLabel = [&layerNames](uint32_t i)
        { return layerNames[i].empty() ? "Layer " + std::to_string(i) : layerNames[i]; };
        if (ImGui::BeginCombo(ui::LabelAbove("Layer"), layerLabel(desc->layer).c_str()))
        {
            for (uint32_t i = 0; i < SceneSettings::kPhysicsLayerCount; ++i)
            {
                if (layerNames[i].empty() && i != desc->layer)
                    continue;
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(layerLabel(i).c_str(), i == desc->layer))
                    ps->SetBodyLayer(node, static_cast<uint8_t>(i));
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ui::ItemTooltip("Physics layer of this body. Scene Settings > Physics Layers names the layers and sets "
                        "which of them collide (trigger overlaps too).");

        bool shapeChanged = false;
        int shapeType = static_cast<int>(desc->shapeType);
        if (ImGui::Combo(ui::LabelAbove("Shape Type"), &shapeType, shapeTypeNames, IM_ARRAYSIZE(shapeTypeNames)))
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
                shapeChanged |= ImGui::DragFloat3(ui::LabelAbove("Half Extents"), &desc->boxHalfExtents.x, 0.01f, 0.001f, 100.0f);
                ui::ItemTooltip("Half-size of the box collider on X, Y, and Z.");
                break;
            case PhysicsShapeType::Sphere:
                shapeChanged |= ImGui::DragFloat(ui::LabelAbove("Radius"), &desc->sphereRadius, 0.01f, 0.001f, 100.0f);
                ui::ItemTooltip("Radius of the sphere collider.");
                break;
            case PhysicsShapeType::Capsule:
                shapeChanged |= ImGui::DragFloat(ui::LabelAbove("Half Height"), &desc->capsuleHalfHeight, 0.01f, 0.001f, 100.0f);
                ui::ItemTooltip("Half the straight section height of the capsule collider.");
                shapeChanged |= ImGui::DragFloat(ui::LabelAbove("Capsule Radius"), &desc->capsuleRadius, 0.01f, 0.001f, 100.0f);
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
            ImGui::DragFloat(ui::LabelAbove("Mass"), &desc->mass, 0.1f, 0.001f, 10000.0f);
        if (desc->bodyType != PhysicsBodyType::Static)
            ui::ItemTooltip("Mass used by dynamic and kinematic body simulation.");

        ImGui::DragFloat(ui::LabelAbove("Friction"), &desc->friction, 0.01f, 0.0f, 1.0f);
        ui::ItemTooltip("Surface resistance applied when this body contacts another.");
        ImGui::DragFloat(ui::LabelAbove("Restitution"), &desc->restitution, 0.01f, 0.0f, 1.0f);
        ui::ItemTooltip("Bounciness applied during collisions.");

        // Joint edits take effect when Play starts (Lua physics.add_joint changes it live).
        ImGui::SeparatorText("Joint");
        PhysicsJointDesc &joint = desc->joint;
        static const char *jointTypeNames[] = {"None", "Fixed", "Hinge", "Distance", "Slider"};
        int jointType = static_cast<int>(joint.type);
        if (ImGui::Combo(ui::LabelAbove("Type##joint"), &jointType, jointTypeNames, IM_ARRAYSIZE(jointTypeNames)))
            joint.type = static_cast<PhysicsJointType>(jointType);
        ui::ItemTooltip("Join this body to another physics body or to the world: Fixed welds, Hinge turns about the "
                        "axis, Distance keeps a length, Slider moves along the axis. Applies when Play starts.");
        if (joint.type == PhysicsJointType::None)
            return;

        if (ImGui::BeginCombo(ui::LabelAbove("Connected Body"),
                              joint.connectedNode.empty() ? "(World)" : joint.connectedNode.c_str()))
        {
            if (ImGui::Selectable("(World)", joint.connectedNode.empty()))
                joint.connectedNode.clear();
            for (uint32_t i = 0; i < scene->GetNodeCount(); ++i)
            {
                NodeId *other = scene->GetNodeId(i);
                if (other == node || !(scene->GetComponentFlags(other) & Component_Physics))
                    continue;
                const std::string &name = scene->GetNodeName(other);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(name.c_str(), name == joint.connectedNode))
                    joint.connectedNode = name;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ui::ItemTooltip("The body this one is joined to, matched by node name, or the world.");

        ImGui::DragFloat3(ui::LabelAbove("Anchor"), &joint.anchor.x, 0.01f);
        ui::ItemTooltip("Joint pivot in this node's local space (0,0,0 = the node origin).");
        const bool hinge = joint.type == PhysicsJointType::Hinge, slider = joint.type == PhysicsJointType::Slider;
        if (hinge || slider)
        {
            ImGui::DragFloat3(ui::LabelAbove("Axis"), &joint.axis.x, 0.01f, -1.0f, 1.0f);
            ui::ItemTooltip(hinge ? "Axis the body turns about, in this node's local space."
                                  : "Axis the body slides along, in this node's local space.");
        }
        if (joint.type == PhysicsJointType::Distance)
        {
            ImGui::DragFloat3(ui::LabelAbove("Connected Anchor"), &joint.connectedAnchor.x, 0.01f);
            ui::ItemTooltip("The other end: local to the connected body, or a world point when joined to the world.");
        }

        if (joint.type != PhysicsJointType::Fixed)
        {
            ImGui::Checkbox("Limits##joint", &joint.limitsEnabled);
            ui::ItemTooltip(hinge    ? "Limit the angle (degrees, -180..0 and 0..180) from the pose when Play starts."
                            : slider ? "Limit the travel (metres, min <= 0 <= max) from the position when Play starts."
                                     : "Allow a length range (rope-like). Off keeps the starting length (a rigid rod).");
            if (joint.limitsEnabled)
                ImGui::DragFloatRange2(ui::LabelAbove(hinge ? "Angle Range" : slider ? "Travel Range"
                                                                                     : "Length Range"),
                                       &joint.limitMin, &joint.limitMax, hinge ? 1.0f : 0.01f, hinge ? -180.0f : -1000.0f,
                                       hinge ? 180.0f : 1000.0f, hinge ? "min %.0f deg" : "min %.2f m",
                                       hinge ? "max %.0f deg" : "max %.2f m");
        }
        if (hinge || slider)
        {
            ImGui::Checkbox("Motor##joint", &joint.motorEnabled);
            ui::ItemTooltip("Drive the joint at a target speed, up to the maximum force.");
            if (joint.motorEnabled)
            {
                ImGui::DragFloat(ui::LabelAbove("Motor Speed"), &joint.motorSpeed, hinge ? 1.0f : 0.01f, 0.0f, 0.0f,
                                 hinge ? "%.1f deg/s" : "%.2f m/s");
                ImGui::DragFloat(ui::LabelAbove("Motor Max Force"), &joint.motorMaxForce, 1.0f, 0.0f, 1e7f,
                                 hinge ? "%.0f N m" : "%.0f N");
            }
        }
        ImGui::DragFloat(ui::LabelAbove("Break Force"), &joint.breakForce, 1.0f, 0.0f, 1e7f,
                         joint.breakForce > 0.0f ? "%.0f N" : "unbreakable");
        ui::ItemTooltip("The joint breaks when it has to hold more than this force (0 = never). Lua physics.on_joint_break reports it.");
    }
} // namespace pe

#endif // PE_PHYSICS
