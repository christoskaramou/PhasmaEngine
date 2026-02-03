#include "SceneView.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "Base/ThreadPool.h"
#include "Camera/Camera.h"
#include "GUI/GUI.h"
#include "GUI/GUIState.h"
#include "GUI/IconsFontAwesome.h"
#include "Scene/Model.h"
#include "Scene/Scene.h"
#include "Scene/SelectionManager.h"
#include "Systems/LightSystem.h"
#include "Systems/RendererSystem.h"
#include "glm/gtx/matrix_decompose.hpp"
#include "imgui/ImGuizmo.h"
#include "imgui/imgui_impl_vulkan.h"
#include "imgui/imgui_internal.h"
#include <filesystem>

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

                DrawGizmos(imageMin, imageSize);

                // Drop Target for SceneView
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                    {
                        const char *pathStr = (const char *)payload->Data;
                        std::filesystem::path path(pathStr);

                        // Check extension
                        std::string ext = path.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                        bool isModel = false;
                        const char *modelExts[] = {".gltf", ".glb", ".obj", ".fbx"};
                        for (const char *e : modelExts)
                        {
                            if (ext == e)
                            {
                                isModel = true;
                                break;
                            }
                        }

                        if (isModel && !GUIState::s_modelLoading)
                        {
                            auto loadTask = [path]()
                            {
                                GUIState::s_modelLoading = true;
                                Model::Load(path);
                                GUIState::s_modelLoading = false;
                            };
                            ThreadPool::GUI.Enqueue(loadTask);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
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
        Camera *camera = scene.GetActiveCamera();

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

    void SceneView::DrawTransformGizmo(const ImVec2 &imageMin, const ImVec2 &imageSize)
    {
        auto &selection = SelectionManager::Instance();
        if (!selection.HasSelection())
            return;

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        Camera *camera = renderer->GetScene().GetActiveCamera();
        if (!camera)
            return;

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(imageMin.x, imageMin.y, imageSize.x, imageSize.y);
        mat4 view = camera->GetView();
        mat4 proj = camera->GetProjectionNoJitter();

        // Vulkan convention
        proj[1][1] *= -1.0f;

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

        mat4 modelMatrix(1.0f);
        bool useLight = false;
        LightType lightType;
        int lightIndex = -1;

        if (selection.GetSelectionType() == SelectionType::Node)
        {
            NodeInfo *nodeInfo = selection.GetSelectedNodeInfo();
            if (!nodeInfo)
                return;
            modelMatrix = nodeInfo->ubo.worldMatrix;
        }
        else if (selection.GetSelectionType() == SelectionType::Light)
        {
            lightType = selection.GetSelectedLightType();
            lightIndex = selection.GetSelectedLightIndex();
            LightSystem *ls = GetGlobalSystem<LightSystem>();
            LightsUBO *lights = ls->GetLights();

            if (!lights)
                return;

            useLight = true;
            if (lightType == LightType::Point && lightIndex >= 0 && lightIndex < MAX_POINT_LIGHTS)
            {
                modelMatrix = translate(mat4(1.0f), vec3(lights->pointLights[lightIndex].position));
            }
            else if (lightType == LightType::Spot && lightIndex >= 0 && lightIndex < MAX_SPOT_LIGHTS)
            {
                vec3 start = vec3(lights->spotLights[lightIndex].position);

                // Compute direction from Rotation
                float p = radians(lights->spotLights[lightIndex].rotation.x);
                float y = radians(lights->spotLights[lightIndex].rotation.y);

                vec3 dir;
                dir.x = cos(p) * sin(y);
                dir.y = sin(p);
                dir.z = cos(p) * cos(y);
                dir = normalize(dir);

                vec3 up = vec3(0, 1, 0);
                if (abs(dot(dir, up)) > 0.99f)
                    up = vec3(1, 0, 0);

                modelMatrix = inverse(lookAt(start, start + dir, up));
            }
            else if (lightType == LightType::Directional)
            {
                // Place directional gizmo in front of camera or at origin
                vec3 pos = vec3(0.0f); // Origin for now
                vec3 dir = vec3(lights->sun.direction);
                vec3 up = vec3(0, 1, 0);
                if (abs(dot(dir, up)) > 0.99f)
                    up = vec3(1, 0, 0);

                modelMatrix = inverse(lookAt(pos, pos + dir, up));
            }
        }
        else
        {
            return;
        }

        mat4 deltaMatrix(1.0f);
        if (ImGuizmo::Manipulate(
                value_ptr(view), value_ptr(proj),
                op, ImGuizmo::WORLD, // World mode simpler for lights for now
                value_ptr(modelMatrix), value_ptr(deltaMatrix)))
        {
            if (useLight)
            {
                LightSystem *ls = GetGlobalSystem<LightSystem>();
                LightsUBO *lights = ls->GetLights();

                vec3 scale, pos, skew;
                vec4 persp;
                quat rot;
                decompose(modelMatrix, scale, rot, pos, skew, persp);

                if (lightType == LightType::Point)
                {
                    lights->pointLights[lightIndex].position = vec4(pos, lights->pointLights[lightIndex].position.w);
                    // Radius managed via UI, not gizmo translation for now
                }
                else if (lightType == LightType::Spot)
                {
                    SpotLight &spot = lights->spotLights[lightIndex];
                    spot.position = vec4(pos, spot.position.w); // update position

                    // Convert Gizmo Rotation (Quat) to Pitch/Yaw
                    vec3 forward = rot * vec3(0, 0, -1);
                    if (length(forward) > 0.001f)
                        forward = normalize(forward);

                    float pitch = degrees(asin(forward.y));
                    float yaw = degrees(atan2(forward.x, forward.z));

                    spot.rotation = vec4(pitch, yaw, spot.rotation.z, spot.rotation.w);
                }
                else if (lightType == LightType::Directional)
                {
                    // Directional light position doesn't matter, only rotation
                    vec3 newDir = rot * vec3(0, 0, -1);
                    lights->sun.direction = vec4(newDir, 0.0f);
                }
            }
            else
            {
                ApplyTransformToNode(selection.GetSelectedNodeInfo(), modelMatrix);
            }
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

    void SceneView::DrawGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize)
    {
        if (GUIState::s_useTransformGizmo)
            DrawTransformGizmo(imageMin, imageSize);

        if (GUIState::s_useLightGizmos)
            DrawLightGizmos(imageMin, imageSize);
    }

    void SceneView::DrawLightGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize)
    {
        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        Scene &scene = renderer->GetScene();
        Camera *camera = scene.GetActiveCamera();

        if (!camera)
            return;

        LightSystem *ls = GetGlobalSystem<LightSystem>();
        LightsUBO *lights = ls->GetLights();

        if (!lights)
            return;

        mat4 view = camera->GetView();
        mat4 proj = camera->GetProjectionNoJitter();
        mat4 viewProj = proj * view;

        auto drawIcon = [&](const vec3 &pos, const char *icon, LightType type, int index)
        {
            vec4 clipPos = viewProj * vec4(pos, 1.0f);

            // Allow clicking if in front of camera
            if (clipPos.w > 0.0f)
            {
                vec2 ndcPos = vec2(clipPos) / clipPos.w;
                float x = (ndcPos.x * 0.5f + 0.5f) * imageSize.x + imageMin.x;
                float y = (ndcPos.y * 0.5f + 0.5f) * imageSize.y + imageMin.y;

                // Color based on selection
                bool isSelected = SelectionManager::Instance().GetSelectedLightType() == type &&
                                  SelectionManager::Instance().GetSelectedLightIndex() == index &&
                                  SelectionManager::Instance().GetSelectionType() == SelectionType::Light;

                float scale = isSelected ? 2.0f : 1.0f;
                ImVec2 textSize = ImGui::CalcTextSize(icon);
                textSize.x *= scale;
                textSize.y *= scale;

                ImVec2 iconPos(x, y);
                iconPos.x -= textSize.x * 0.5f;
                iconPos.y -= textSize.y * 0.5f;

                ImGui::SetCursorScreenPos(iconPos);

                ImVec4 color = isSelected ? ImVec4(1, 1, 0, 1) : ImVec4(1, 1, 1, 1);
                ImGui::PushStyleColor(ImGuiCol_Text, color);

                ImGui::SetWindowFontScale(scale);
                ImGui::Text("%s", icon);
                ImGui::SetWindowFontScale(1.0f);

                ImGui::SetCursorScreenPos(iconPos);
                ImGui::InvisibleButton(std::string("##LightIcon" + std::to_string((int)type) + "_" + std::to_string(index)).c_str(), textSize);

                if (ImGui::IsItemClicked())
                {
                    SelectionManager::Instance().Select(type, index);
                }

                ImGui::PopStyleColor();
            }
        };

        // Point Lights
        for (int i = 0; i < MAX_POINT_LIGHTS; i++)
        {
            drawIcon(vec3(lights->pointLights[i].position), ICON_FA_LIGHTBULB, LightType::Point, i);
        }

        // Spot Lights
        for (int i = 0; i < MAX_SPOT_LIGHTS; i++)
        {
            drawIcon(vec3(lights->spotLights[i].position), ICON_FA_LIGHTBULB, LightType::Spot, i);
        }
    }
} // namespace pe
