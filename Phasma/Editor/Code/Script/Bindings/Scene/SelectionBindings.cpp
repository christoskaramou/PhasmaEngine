#include "Script/ScriptSystem.h"
#include "Script/Bindings/Lerp/Tween.h"
#include "Camera/Camera.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SelectionManager.h"

namespace pe
{
    // Camera position that frames the current selection (pulled back along the camera
    // front by twice the selection radius). Returns nullopt when nothing focusable is
    // selected. Used by selection.focus() to glide the camera there via TweenCameraTo.
    static std::optional<vec3> ComputeFocusTarget()
    {
        Scene *scene = GetActiveScene();
        if (!scene)
            return std::nullopt;
        Camera *cam = scene->GetActiveCamera();
        if (!cam)
            return std::nullopt;

        auto &sel = SelectionManager::Instance();
        vec3 center;
        float radius = 1.0f;

        if (sel.GetSelectionType() == SelectionType::Node ||
            sel.GetSelectionType() == SelectionType::Mesh)
        {
            NodeId *node = sel.GetSelectedNode();
            if (!node)
                return std::nullopt;
            const AABB &bb = scene->GetWorldAABB(node);
            center = bb.GetCenter();
            radius = glm::length(bb.GetSize()) * 0.5f;
        }
        else if (sel.GetSelectionType() == SelectionType::Light)
        {
            int idx = sel.GetSelectedLightIndex();
            if (sel.GetSelectedLightType() == LightType::Point)
            {
                auto &lights = scene->GetPointLights();
                if (idx < 0 || idx >= static_cast<int>(lights.size()))
                    return std::nullopt;
                center = vec3(lights[idx].position);
            }
            else
            {
                auto &lights = scene->GetDirectionalLights();
                if (idx < 0 || idx >= static_cast<int>(lights.size()))
                    return std::nullopt;
                center = vec3(lights[idx].position);
            }
        }
        else
        {
            return std::nullopt;
        }

        vec3 dir = glm::normalize(cam->GetFront());
        return center - dir * (radius * 2.0f);
    }

    static struct SelectionBindings
    {
        SelectionBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table selection = lua["selection"].get_or_create<sol::table>();

                selection.set_function("get", [](sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    auto &sel = SelectionManager::Instance();
                    sol::table t = lua.create_table();
                    t["has_selection"] = sel.HasSelection();
                    NodeId *node = sel.GetSelectedNode();
                    if (node)
                        t["node_index"] = node->index;
                    else
                        t["node_index"] = sol::nil;
                    const char *typeStr = "none";
                    switch (sel.GetSelectionType())
                    {
                    case SelectionType::Node: typeStr = "node"; break;
                    case SelectionType::Mesh: typeStr = "mesh"; break;
                    case SelectionType::Camera: typeStr = "camera"; break;
                    case SelectionType::Light: typeStr = "light"; break;
                    case SelectionType::Emitter: typeStr = "emitter"; break;
                    }
                    t["type"] = typeStr;
                    return t;
                });

                selection.set_function("select_node", [](int nodeIndex) {
                    Scene *scene = GetActiveScene();
                    if (!scene) return;
                    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(scene->GetNodeCount())) return;
                    SelectionManager::Instance().Select(scene->GetNodeId(nodeIndex), SelectionType::Node);
                });

                selection.set_function("select_mesh", [](int nodeIndex) {
                    Scene *scene = GetActiveScene();
                    if (!scene) return;
                    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(scene->GetNodeCount())) return;
                    SelectionManager::Instance().Select(scene->GetNodeId(nodeIndex), SelectionType::Mesh);
                });

                selection.set_function("clear", []() {
                    SelectionManager::Instance().ClearSelection();
                });

                // Glide the active camera to frame the selection (smooth focus, via the
                // shared tween system). Bound to the editor F key.
                selection.set_function("focus", []() {
                    if (auto target = ComputeFocusTarget())
                        TweenCameraTo(*target, 0.3f);
                });

                selection.set_function("get_gizmo", []() -> std::string {
                    switch (SelectionManager::Instance().GetGizmoOperation())
                    {
                    case GizmoOperation::Translate: return "translate";
                    case GizmoOperation::Rotate: return "rotate";
                    case GizmoOperation::Scale: return "scale";
                    default: return "translate";
                    }
                });

                selection.set_function("set_gizmo", [](const std::string &op) {
                    auto &sel = SelectionManager::Instance();
                    if (op == "translate") sel.SetGizmoOperation(GizmoOperation::Translate);
                    else if (op == "rotate") sel.SetGizmoOperation(GizmoOperation::Rotate);
                    else if (op == "scale") sel.SetGizmoOperation(GizmoOperation::Scale);
                }); });
        }
    } s_selectionBindings;
} // namespace pe
