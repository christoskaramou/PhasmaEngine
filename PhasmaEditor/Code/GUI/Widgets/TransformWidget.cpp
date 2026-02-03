#include "TransformWidget.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Base/Path.h"
#include "Scene/Model.h"
#include "Scene/SelectionManager.h"
#include "glm/gtx/matrix_decompose.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_vulkan.h"
#include "imgui/imgui_internal.h"

namespace pe
{
    TransformWidget::TransformWidget() : Widget("Transform")
    {
    }

    TransformWidget::~TransformWidget()
    {
        Image::Destroy(m_translateImage);
        Image::Destroy(m_rotateImage);
        Image::Destroy(m_scaleImage);
    }

    void TransformWidget::Init(GUI *gui)
    {
        Widget::Init(gui);

        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();

        auto LoadIcon = [&](const std::string &path, Image *&outImage) -> void *
        {
            if (!std::filesystem::exists(path))
                return nullptr;

            Image *img = Image::LoadRGBA8(cmd, path);
            if (!img)
                return nullptr;

            outImage = img; // Store for cleanup

            img->CreateSRV(vk::ImageViewType::e2D);
            if (!img->GetSampler())
            {
                vk::SamplerCreateInfo info = Sampler::CreateInfoInit();
                info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
                info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
                img->SetSampler(Sampler::Create(info));
            }

            ImageBarrierInfo barrier{};
            barrier.image = img;
            barrier.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.stageFlags = vk::PipelineStageFlagBits2::eFragmentShader;
            barrier.accessMask = vk::AccessFlagBits2::eShaderRead;
            barrier.baseMipLevel = 0;
            barrier.mipLevels = img->GetMipLevels();
            barrier.baseArrayLayer = 0;
            barrier.arrayLayers = img->GetArrayLayers();

            cmd->ImageBarrier(barrier);

            return ImGui_ImplVulkan_AddTexture((VkSampler)img->GetSampler()->ApiHandle(), (VkImageView)img->GetSRV()->ApiHandle(), (VkImageLayout)vk::ImageLayout::eShaderReadOnlyOptimal);
        };

        m_translateIcon = LoadIcon(Path::Assets + "Icons/translate_icon_v2.png", m_translateImage);
        m_rotateIcon = LoadIcon(Path::Assets + "Icons/rotate_icon_v2.png", m_rotateImage);
        m_scaleIcon = LoadIcon(Path::Assets + "Icons/scale_icon_v2.png", m_scaleImage);

        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        queue->WaitIdle();
        cmd->Return();
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
            vec3 eulerRot = degrees(eulerAngles(rotation));
            ApplyLocalTransform(node, position, eulerRot, scl);
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
            ApplyLocalTransform(node, position, eulerDeg, scl);
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
            ApplyLocalTransform(node, position, eulerDeg, scl);
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

        // Draw Icon instead of text
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float h = ImGui::GetFrameHeight();
        float iconSize = h;

        // Center icon in the column
        float availW = ImGui::GetColumnWidth();
        float startX = p.x + (availW - iconSize) * 0.5f;

        if (type == TransformType::Position && m_translateIcon)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - iconSize) * 0.5f);
            ImGui::Image((ImTextureID)m_translateIcon, ImVec2(iconSize, iconSize));
        }
        else if (type == TransformType::Rotation && m_rotateIcon)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - iconSize) * 0.5f);
            ImGui::Image((ImTextureID)m_rotateIcon, ImVec2(iconSize, iconSize));
        }
        else if (type == TransformType::Scale && m_scaleIcon)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - iconSize) * 0.5f);
            ImGui::Image((ImTextureID)m_scaleIcon, ImVec2(iconSize, iconSize));
        }
        else
        {
            float centerY = p.y + h * 0.5f;
            float drawX = p.x + 10.0f;
            ImU32 iconColor = ImGui::GetColorU32(ImGuiCol_Text);

            if (type == TransformType::Position)
            {
                // 4-way arrow icon (fallback)
                float r = h * 0.3f;
                float cx = drawX + h * 0.5f;
                dl->AddLine(ImVec2(cx - r, centerY), ImVec2(cx + r, centerY), iconColor, 2.0f);
                dl->AddLine(ImVec2(cx, centerY - r), ImVec2(cx, centerY + r), iconColor, 2.0f);
            }
            else if (type == TransformType::Rotation)
            {
                float cx = drawX + h * 0.5f;
                dl->AddCircle(ImVec2(cx, centerY), h * 0.3f, iconColor, 0, 2.0f);
            }
            else if (type == TransformType::Scale)
            {
                float cx = drawX + h * 0.5f;
                dl->AddRect(ImVec2(cx - 5, centerY - 5), ImVec2(cx + 5, centerY + 5), iconColor, 0, 0, 2.0f);
            }
            ImGui::Dummy(ImVec2(columnWidth, h));
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

    void TransformWidget::ApplyLocalTransform(NodeInfo *nodeInfo, const vec3 &pos, const vec3 &rot, const vec3 &scl)
    {
        auto &selection = SelectionManager::Instance();
        Model *model = selection.GetSelectedModel();
        if (!model)
            return;

        // Directly set localMatrix from TRS (no parent chain needed!)
        mat4 translationMat = translate(mat4(1.0f), pos);
        mat4 rotationMat = mat4(quat(radians(rot)));
        mat4 scaleMat = scale(mat4(1.0f), scl);
        nodeInfo->localMatrix = translationMat * rotationMat * scaleMat;

        model->MarkDirty(selection.GetSelectedNodeIndex());
    }
} // namespace pe
