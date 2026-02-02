#include "SceneView.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "Camera/Camera.h"
#include "GUI/GUI.h"
#include "GUI/GUIState.h"
#include "Scene/Model.h"
#include "Scene/Scene.h"
#include "Scene/SelectionManager.h"
#include "Systems/RendererSystem.h"
#include "imgui/ImGuizmo.h"
#include "imgui/imgui_impl_vulkan.h"
#include "imgui/imgui_internal.h"

namespace pe
{
    void SceneView::Init(GUI *gui)
    {
        Widget::Init(gui);
    }

    void SceneView::Update()
    {
        if (GUIState::s_sceneViewFloating)
        {
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        }
        else if (GUIState::s_sceneViewRedockQueued && m_gui->GetDockspaceId() != 0)
        {
            ImGui::DockBuilderDockWindow("Scene View", m_gui->GetDockspaceId());
            GUIState::s_sceneViewRedockQueued = false;
        }

        ImGuiWindowFlags sceneViewFlags = 0;
        if (GUIState::s_sceneViewFloating)
            sceneViewFlags |= ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Scene View", nullptr, sceneViewFlags);
        ImGui::PopStyleVar();

        GUIState::s_sceneViewFocused = ImGui::IsWindowFocused();

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        Image *displayRT = renderer->GetDisplayRT();

        auto recreateSceneTexture = [renderer, displayRT]()
        {
            if (!displayRT)
                return;

            Image::Destroy(GUIState::s_sceneViewImage);
            GUIState::s_sceneViewImage = renderer->CreateFSSampledImage(false);

            if (GUIState::s_viewportTextureId)
            {
                ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)GUIState::s_viewportTextureId);
                GUIState::s_viewportTextureId = nullptr;
            }
        };

        if (!GUIState::s_sceneViewImage || !displayRT ||
            GUIState::s_sceneViewImage->GetWidth() != displayRT->GetWidth() ||
            GUIState::s_sceneViewImage->GetHeight() != displayRT->GetHeight())
        {
            recreateSceneTexture();
        }

        Image *sceneTexture = GUIState::s_sceneViewImage ? GUIState::s_sceneViewImage : displayRT;

        if (!sceneTexture || !sceneTexture->HasSRV() || !sceneTexture->GetSampler())
        {
            ImGui::TextDisabled("Initializing viewport...");
            ImGui::End();
            return;
        }

        if (!GUIState::s_viewportTextureId)
        {
            VkSampler sampler = sceneTexture->GetSampler()->ApiHandle();
            VkImageView imageView = sceneTexture->GetSRV()->ApiHandle();

            if (sampler && imageView)
                GUIState::s_viewportTextureId = (void *)ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        if (GUIState::s_viewportTextureId)
        {
            ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();

            if (viewportPanelSize.x > 0 && viewportPanelSize.y > 0)
            {
                float targetAspect = (float)sceneTexture->GetWidth() / (float)sceneTexture->GetHeight();
                float panelAspect = viewportPanelSize.x / viewportPanelSize.y;

                ImVec2 imageSize;
                if (panelAspect > targetAspect)
                {
                    imageSize.y = viewportPanelSize.y;
                    imageSize.x = imageSize.y * targetAspect;
                }
                else
                {
                    imageSize.x = viewportPanelSize.x;
                    imageSize.y = imageSize.x / targetAspect;
                }

                ImVec2 cursorPos = ImGui::GetCursorPos();
                cursorPos.x += (viewportPanelSize.x - imageSize.x) * 0.5f;
                cursorPos.y += (viewportPanelSize.y - imageSize.y) * 0.5f;
                ImGui::SetCursorPos(cursorPos);

                ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(GUIState::s_viewportTextureId)), imageSize);

                ImVec2 imageMin = ImGui::GetItemRectMin();
                ImVec2 imageMax = ImGui::GetItemRectMax();

                if (!ImGuizmo::IsOver())
                {
                    ImGui::SetCursorPos(cursorPos);
                    ImGui::InvisibleButton("##SceneViewInput", imageSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                        ImGui::SetWindowFocus();

                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                    {
                        ImVec2 mousePos = ImGui::GetMousePos();
                        float normalizedX = (mousePos.x - imageMin.x) / (imageMax.x - imageMin.x);
                        float normalizedY = (mousePos.y - imageMin.y) / (imageMax.y - imageMin.y);

                        PerformObjectPicking(normalizedX, normalizedY);
                    }
                }

                DrawGizmo(imageMin, imageSize);
            }
        }
        else
        {
            ImGui::TextDisabled("Rendering...");
        }

        ImGui::End();
    }

    void SceneView::PerformObjectPicking(float normalizedX, float normalizedY)
    {
        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        Scene &scene = renderer->GetScene();
        Camera *camera = scene.GetCamera(0);

        if (!camera)
            return;

        // Convert screen [0,1] to NDC [-1,1]
        float ndcX = normalizedX * 2.0f - 1.0f;
        float ndcY = normalizedY * 2.0f - 1.0f;

        mat4 invViewProj = camera->GetInvViewProjection();

        // Unproject near and far points (reverse-Z: near=1, far=0)
        vec4 nearPoint = invViewProj * vec4(ndcX, ndcY, 1.0f, 1.0f);
        nearPoint /= nearPoint.w;

        vec4 farPoint = invViewProj * vec4(ndcX, ndcY, 0.0f, 1.0f);
        farPoint /= farPoint.w;

        vec3 rayOrigin = vec3(nearPoint);
        vec3 rayDir = normalize(vec3(farPoint) - vec3(nearPoint));

        Model *hitModel = nullptr;
        int hitNodeIndex = -1;
        float closestT = std::numeric_limits<float>::max();

        auto &models = scene.GetModels();
        for (auto model : models)
        {
            if (!model || !model->IsRenderReady())
                continue;

            const auto &nodeInfos = model->GetNodeInfos();
            for (int i = 0; i < static_cast<int>(nodeInfos.size()); i++)
            {
                if (model->GetNodeMesh(i) < 0)
                    continue;

                const NodeInfo &nodeInfo = nodeInfos[i];
                const AABB &aabb = nodeInfo.worldBoundingBox;

                vec3 minBound = aabb.min;
                vec3 maxBound = aabb.max;

                vec3 invDir = 1.0f / rayDir;
                vec3 t1 = (minBound - rayOrigin) * invDir;
                vec3 t2 = (maxBound - rayOrigin) * invDir;

                vec3 tMin = min(t1, t2);
                vec3 tMax = max(t1, t2);

                float tNear = max(max(tMin.x, tMin.y), tMin.z);
                float tFar = min(min(tMax.x, tMax.y), tMax.z);

                if (tNear <= tFar && tFar >= 0.0f)
                {
                    float t = tNear >= 0.0f ? tNear : tFar;
                    if (t < closestT)
                    {
                        closestT = t;
                        hitModel = model;
                        hitNodeIndex = i;
                    }
                }
            }
        }

        auto &selection = SelectionManager::Instance();
        if (hitModel)
        {
            selection.Select(hitModel, hitNodeIndex);
        }
        else
        {
            selection.ClearSelection();
        }
    }

    void SceneView::DrawGizmo(const ImVec2 &imageMin, const ImVec2 &imageSize)
    {
        auto &selection = SelectionManager::Instance();
        if (!selection.HasSelection())
            return;

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        Camera *camera = renderer->GetScene().GetCamera(0);
        if (!camera)
            return;

        NodeInfo *nodeInfo = selection.GetSelectedNodeInfo();
        if (!nodeInfo)
            return;

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(imageMin.x, imageMin.y, imageSize.x, imageSize.y);
        mat4 view = camera->GetView();
        mat4 proj = camera->GetProjectionNoJitter();

        // Vulkan convention
        proj[1][1] *= -1.0f;

        mat4 modelMatrix = nodeInfo->ubo.worldMatrix;

        ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
        switch (selection.GetGizmoOperation())
        {
        case GizmoOperation::Rotate:
            op = ImGuizmo::ROTATE;
            break;
        case GizmoOperation::Scale:
            op = ImGuizmo::SCALE;
            break;
        default:
            op = ImGuizmo::TRANSLATE;
            break;
        }

        mat4 deltaMatrix(1.0f);
        if (ImGuizmo::Manipulate(
                value_ptr(view), value_ptr(proj),
                op, ImGuizmo::WORLD,
                value_ptr(modelMatrix), value_ptr(deltaMatrix)))
        {
            ApplyTransformToNode(nodeInfo, modelMatrix);
        }
    }

    void SceneView::ApplyTransformToNode(NodeInfo *nodeInfo, const mat4 &newWorldMatrix)
    {
        if (!nodeInfo)
            return;

        auto &selection = SelectionManager::Instance();
        Model *model = selection.GetSelectedModel();
        if (!model)
            return;

        int selectedNodeIndex = selection.GetSelectedNodeIndex();
        auto &nodeInfos = model->GetNodeInfos();

        int parentIdx = nodeInfo->parent;
        mat4 parentWorldMatrix = (parentIdx >= 0) ? nodeInfos[parentIdx].ubo.worldMatrix : model->GetMatrix();
        nodeInfo->localMatrix = inverse(parentWorldMatrix) * newWorldMatrix;

        model->MarkDirty(selectedNodeIndex);
    }
} // namespace pe
