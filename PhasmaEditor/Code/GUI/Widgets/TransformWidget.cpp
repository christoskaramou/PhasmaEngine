#include "TransformWidget.h"
#include "GUI/IconsFontAwesome.h"
#include "Scene/Model.h"
#include "Scene/SelectionManager.h"
#include "TransformWidget.h"
#include "glm/gtx/matrix_decompose.hpp"
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

    void TransformWidget::DrawEmbed(NodeInfo *nodeInfo)
    {
        if (ImGui::CollapsingHeader("Node Info", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawNodeInfo(nodeInfo);
        }

        ImGui::Separator();

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawGizmoModeButtons();
            ImGui::Text("Local Transform");
            DrawPositionEditor(nodeInfo);
            DrawRotationEditor(nodeInfo);
            DrawScaleEditor(nodeInfo);
        }
    }

    void TransformWidget::DrawNodeInfo(NodeInfo *node)
    {
        if (!node)
            return;

        char buffer[256];
        memset(buffer, 0, 256);
        memcpy(buffer, node->name.c_str(), std::min(node->name.length(), sizeof(buffer) - 1));
        if (ImGui::InputText("Name", buffer, 256))
        {
            node->name = buffer;
        }

        if (ImGui::BeginTable("NodeInfoTypes", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Parent Index");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", node->parent);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Children Count");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%zu", node->children.size());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Data Offset");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%zu", node->dataOffset);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Indirect Index");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", node->indirectIndex);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Instance Index");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", node->instanceIndex);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Is Dirty");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", node->dirty ? "true" : "false");

            ImGui::EndTable();
        }

        if (ImGui::CollapsingHeader("World AABB"))
        {
            ImGui::LabelText("Min", "(%.2f, %.2f, %.2f)", node->worldBoundingBox.min.x, node->worldBoundingBox.min.y, node->worldBoundingBox.min.z);
            ImGui::LabelText("Max", "(%.2f, %.2f, %.2f)", node->worldBoundingBox.max.x, node->worldBoundingBox.max.y, node->worldBoundingBox.max.z);
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

    void TransformWidget::DrawPositionEditor(NodeInfo *node)
    {
        // Decompose LOCAL matrix (relative to parent)
        vec3 position, scl, skew;
        quat rotation;
        vec4 perspective;
        decompose(node->localMatrix, scl, rotation, position, skew, perspective);

        vec3 oldPos = position;
        DrawVec3Control(TransformType::Position, position);
        if (position != oldPos)
        {
            ApplyLocalTransform(node, position, rotation, scl);
        }
    }

    void TransformWidget::DrawRotationEditor(NodeInfo *node)
    {
        vec3 position, scl, skew;
        quat rotation;
        vec4 perspective;
        decompose(node->localMatrix, scl, rotation, position, skew, perspective);

        vec3 eulerDeg = degrees(eulerAngles(rotation));
        vec3 oldRot = eulerDeg;

        DrawVec3Control(TransformType::Rotation, eulerDeg);
        if (eulerDeg != oldRot)
        {
            ApplyLocalTransform(node, position, quat(radians(eulerDeg)), scl);
        }
    }

    void TransformWidget::DrawScaleEditor(NodeInfo *node)
    {
        vec3 position, scl, skew;
        quat rotation;
        vec4 perspective;
        decompose(node->localMatrix, scl, rotation, position, skew, perspective);

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

    void TransformWidget::ApplyLocalTransform(NodeInfo *nodeInfo, const vec3 &pos, const quat &rot, const vec3 &scl)
    {
        auto &selection = SelectionManager::Instance();
        Model *model = selection.GetSelectedModel();
        if (!model)
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
        nodeInfo->localMatrix = translationMat * rotationMat * scaleMat;

        model->MarkDirty(selection.GetSelectedNodeIndex());
    }
} // namespace pe
