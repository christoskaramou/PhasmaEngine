#include "TransformWidget.h"
#include "GUI/IconsFontAwesome.h"
#include "Scene/Scene.h"
#include "Scene/SelectionManager.h"
#include "Systems/RendererSystem.h"
#include "glm/gtx/matrix_decompose.hpp"
#include "imgui/ImGuizmo.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

namespace pe
{
    TransformWidget::TransformWidget() : Widget("Transform")
    {
    }

    TransformWidget::~TransformWidget()
    {
    }

    void TransformWidget::Init(GUI *gui)
    {
        Widget::Init(gui);
    }

    void TransformWidget::Update()
    {
    }

    void TransformWidget::DrawEmbed(NodeId *node)
    {
        if (ImGui::CollapsingHeader("Node Info", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawNodeInfo(node);
        }

        ImGui::Separator();

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawGizmoModeButtons();
            ImGui::Text("Local Transform");
            DrawPositionEditor(node);
            DrawRotationEditor(node);
            DrawScaleEditor(node);
        }
    }

    void TransformWidget::DrawNodeInfo(NodeId *node)
    {
        if (!node)
            return;

        Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();

        char buffer[256];
        memset(buffer, 0, 256);
        const std::string &nodeName = scene.GetNodeName(node);
        memcpy(buffer, nodeName.c_str(), std::min(nodeName.length(), sizeof(buffer) - 1));
        if (ImGui::InputText("Name", buffer, 256))
        {
            scene.SetNodeName(node, buffer);
        }

        if (ImGui::BeginTable("NodeInfoTypes", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Parent");
            ImGui::TableSetColumnIndex(1);
            NodeId *parent = scene.GetParent(node);
            if (parent)
                ImGui::Text("%s", scene.GetNodeName(parent).c_str());
            else
                ImGui::Text("(root)");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Children Count");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%zu", scene.GetChildren(node).size());

            ImGui::EndTable();
        }

        if (ImGui::CollapsingHeader("World AABB"))
        {
            const AABB &worldBounds = scene.GetWorldAABB(node);
            ImGui::LabelText("Min", "(%.2f, %.2f, %.2f)", worldBounds.min.x, worldBounds.min.y, worldBounds.min.z);
            ImGui::LabelText("Max", "(%.2f, %.2f, %.2f)", worldBounds.max.x, worldBounds.max.y, worldBounds.max.z);
        }
    }

    void TransformWidget::DrawGizmoModeButtons()
    {
        auto &selection = SelectionManager::Instance();
        GizmoOperation currentOp = selection.GetGizmoOperation();

        bool isTranslate = (currentOp == GizmoOperation::Translate);
        if (isTranslate)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button("Translate (W)"))
            selection.SetGizmoOperation(GizmoOperation::Translate);
        if (isTranslate)
            ImGui::PopStyleColor();

        ImGui::SameLine();

        bool isRotate = (currentOp == GizmoOperation::Rotate);
        if (isRotate)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button("Rotate (E)"))
            selection.SetGizmoOperation(GizmoOperation::Rotate);
        if (isRotate)
            ImGui::PopStyleColor();

        ImGui::SameLine();

        bool isScale = (currentOp == GizmoOperation::Scale);
        if (isScale)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button("Scale (R)"))
            selection.SetGizmoOperation(GizmoOperation::Scale);
        if (isScale)
            ImGui::PopStyleColor();
    }

    void TransformWidget::DrawPositionEditor(NodeId *node)
    {
        Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();
        const mat4 &localMatrix = scene.GetLocalMatrix(node);

        vec3 position, scl, skew;
        quat rotation;
        vec4 perspective;
        decompose(localMatrix, scl, rotation, position, skew, perspective);

        vec3 oldPos = position;
        DrawVec3Control(TransformType::Position, position);
        if (position != oldPos)
        {
            ApplyLocalTransform(node, position, rotation, scl);
        }
    }

    void TransformWidget::DrawRotationEditor(NodeId *node)
    {
        Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();
        const mat4 &localMatrix = scene.GetLocalMatrix(node);

        float t[3], r[3], s[3];
        ImGuizmo::DecomposeMatrixToComponents(value_ptr(localMatrix), t, r, s);
        vec3 eulerDeg = vec3(r[0], r[1], r[2]);
        vec3 oldRot = eulerDeg;

        DrawVec3Control(TransformType::Rotation, eulerDeg);
        if (glm::any(glm::notEqual(eulerDeg, oldRot, 0.001f)))
        {
            float matrix[16];
            ImGuizmo::RecomposeMatrixFromComponents(t, value_ptr(eulerDeg), s, matrix);
            scene.SetLocalMatrix(node, make_mat4(matrix));
        }
    }

    void TransformWidget::DrawScaleEditor(NodeId *node)
    {
        Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();
        const mat4 &localMatrix = scene.GetLocalMatrix(node);

        vec3 position, scl, skew;
        quat rotation;
        vec4 perspective;
        decompose(localMatrix, scl, rotation, position, skew, perspective);

        vec3 eulerDeg = degrees(eulerAngles(rotation));
        vec3 oldScale = scl;

        DrawVec3Control(TransformType::Scale, scl, 1.0f);
        if (scl != oldScale)
        {
            scl = max(scl, vec3(0.001f));
            ApplyLocalTransform(node, position, rotation, scl);
        }
    }

    void TransformWidget::DrawVec3Control(TransformType type, vec3 &values, float resetValue, float columnWidth)
    {
        std::string label;
        switch (type)
        {
        case TransformType::Position:
            label = "##Position";
            break;
        case TransformType::Rotation:
            label = "##Rotation";
            break;
        case TransformType::Scale:
            label = "##Scale";
            break;
        }

        ImGui::PushID(label.c_str());

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);

        if (type == TransformType::Position)
        {
            ImGui::Text(ICON_FA_ARROWS_ALT "  Position");
        }
        else if (type == TransformType::Rotation)
        {
            ImGui::Text(ICON_FA_SYNC_ALT "  Rotation");
        }
        else if (type == TransformType::Scale)
        {
            ImGui::Text(ICON_FA_EXPAND "  Scale");
        }

        ImGui::NextColumn();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{5, 0});
        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());

        // X
        ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        // Y
        ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        // Z
        ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();

        ImGui::PopStyleVar();
        ImGui::Columns(1);
        ImGui::PopID();
    }

    void TransformWidget::ApplyLocalTransform(NodeId *node, const vec3 &pos, const quat &rot, const vec3 &scl)
    {
        if (!node)
            return;

        // Guard against NaN
        if (glm::any(glm::isnan(pos)) || glm::any(glm::isinf(pos)))
            return;
        if (glm::any(glm::isnan(rot)) || glm::any(glm::isinf(rot)))
            return;
        if (glm::any(glm::isnan(scl)) || glm::any(glm::isinf(scl)))
            return;

        // Directly set localMatrix from TRS
        mat4 translationMat = translate(mat4(1.0f), pos);
        mat4 rotationMat = mat4(rot);
        mat4 scaleMat = scale(mat4(1.0f), scl);
        const mat4 localMatrix = translationMat * rotationMat * scaleMat;

        Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();
        scene.SetLocalMatrix(node, localMatrix);
    }
} // namespace pe
