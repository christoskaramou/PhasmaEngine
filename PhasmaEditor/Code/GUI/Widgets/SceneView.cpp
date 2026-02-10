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
            ImGui::DockBuilderDockWindow("Viewport", m_gui->GetDockspaceId());
            GUIState::s_sceneViewRedockQueued = false;
        }

        ImGuiWindowFlags sceneViewFlags = 0;
        if (GUIState::s_sceneViewFloating)
            sceneViewFlags |= ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Viewport", nullptr, sceneViewFlags);
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
                bool isImageHovered = ImGui::IsItemHovered();

                // Draw Gizmos (hit tests are done inside Manipulate)
                DrawGizmos(imageMin, imageSize);

                bool isOverGizmo = ImGuizmo::IsOver();
                bool isUsingGizmo = ImGuizmo::IsUsing();

                if (!isOverGizmo && !isUsingGizmo)
                {
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && isImageHovered)
                    {
                        ImVec2 mousePos = ImGui::GetMousePos();
                        float normalizedX = (mousePos.x - imageMin.x) / (imageMax.x - imageMin.x);
                        float normalizedY = (mousePos.y - imageMin.y) / (imageMax.y - imageMin.y);

                        PerformObjectPicking(normalizedX, normalizedY);
                    }

                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && isImageHovered)
                    {
                        ImGui::SetWindowFocus();
                    }
                }

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
        vec3 rayOrigin = vec3(nearPoint);
        vec3 rayDir;

        if (abs(farPoint.w) < 1e-6f)
        {
            rayDir = normalize(vec3(farPoint));
        }
        else
        {
            farPoint /= farPoint.w;
            rayDir = normalize(vec3(farPoint) - vec3(nearPoint));
        }
        vec3 camPos = camera->GetPosition();

        struct Intersection
        {
            float t;
            Model *model;
            int nodeIndex;
        };
        std::vector<Intersection> intersections;

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

                // Check if camera is inside the AABB
                if (camPos.x >= minBound.x && camPos.x <= maxBound.x &&
                    camPos.y >= minBound.y && camPos.y <= maxBound.y &&
                    camPos.z >= minBound.z && camPos.z <= maxBound.z)
                {
                    continue;
                }

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
                    intersections.push_back({t, model, i});
                }
            }
        }

        auto &selection = SelectionManager::Instance();

        if (intersections.empty())
        {
            selection.ClearSelection();
            return;
        }

        // Sort by distance
        std::sort(intersections.begin(), intersections.end(), [](const Intersection &a, const Intersection &b)
                  { return a.t < b.t; });

        int selectIndex = 0;

        // Cyclic selection logic
        if (selection.HasSelection() && selection.GetSelectionType() == SelectionType::Node)
        {
            Model *currentModel = selection.GetSelectedModel();
            int currentNodeIndex = selection.GetSelectedNodeIndex();

            for (int i = 0; i < static_cast<int>(intersections.size()); i++)
            {
                if (intersections[i].model == currentModel && intersections[i].nodeIndex == currentNodeIndex)
                {
                    selectIndex = (i + 1) % intersections.size();
                    break;
                }
            }
        }

        selection.Select(intersections[selectIndex].model, intersections[selectIndex].nodeIndex);
    }

    vec3 GetDirectionFromRotation(const vec4 &rotation)
    {
        quat q = quat(rotation.w, rotation.x, rotation.y, rotation.z);
        vec3 dir = q * vec3(0, 0, -1);

        return glm::normalize(dir);
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
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(imageMin.x, imageMin.y, imageSize.x, imageSize.y);

        mat4 view = camera->GetView();

        // Use a standard projection matrix for ImGuizmo to avoid issues with infinite/reverse-Z
        float fovY = camera->Fovy();
        float aspect = camera->GetAspect();
        float nearPlane = camera->GetNearPlane();
        // Standard perspective (Right-handed for GLM/ImGuizmo default)
        mat4 proj = perspective(fovY, aspect, nearPlane, 1000.0f);
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

        ImGuizmo::MODE mode = ImGuizmo::WORLD; // World mode simpler for lights for now

        mat4 matrix(1.0f);
        bool useLight = false;
        LightType lightType;
        int lightIndex = -1;

        if (selection.GetSelectionType() == SelectionType::Node)
        {
            NodeInfo *nodeInfo = selection.GetSelectedNodeInfo();
            if (!nodeInfo)
                return;
            matrix = nodeInfo->ubo.worldMatrix;
        }
        else if (selection.GetSelectionType() == SelectionType::Light)
        {
            lightType = selection.GetSelectedLightType();
            lightIndex = selection.GetSelectedLightIndex();
            LightSystem *ls = GetGlobalSystem<LightSystem>();

            if (!ls)
                return;

            useLight = true;
            if (lightType == LightType::Point && lightIndex >= 0 && lightIndex < (int)ls->GetPointLights().size())
            {
                matrix = translate(mat4(1.0f), vec3(ls->GetPointLights()[lightIndex].position));
            }
            else if (lightType == LightType::Spot && lightIndex >= 0 && lightIndex < (int)ls->GetSpotLights().size())
            {
                SpotLight &spot = ls->GetSpotLights()[lightIndex];
                vec3 start = vec3(spot.position);
                quat rot = quat(spot.rotation.w, spot.rotation.x, spot.rotation.y, spot.rotation.z);
                matrix = translate(mat4(1.0f), start) * mat4_cast(rot);
            }
            else if (lightType == LightType::Directional)
            {
                if (!ls->GetDirectionalLights().empty())
                {
                    DirectionalLight &dirLight = ls->GetDirectionalLights()[0];
                    vec3 dir = vec3(dirLight.direction);
                    vec3 pos = vec3(dirLight.position);
                    
                    // Create a rotation that points along 'dir'
                    quat rot = rotation(vec3(0, 0, -1), dir); // Rotate from -Z to 'dir'
                    matrix = translate(mat4(1.0f), pos) * mat4_cast(rot);
                }
            }
            else if (lightType == LightType::Area && lightIndex >= 0 && lightIndex < (int)ls->GetAreaLights().size())
            {
                AreaLight &area = ls->GetAreaLights()[lightIndex];
                vec3 start = vec3(area.position);
                quat rot = quat(area.rotation.w, area.rotation.x, area.rotation.y, area.rotation.z);
                matrix = translate(mat4(1.0f), start) * mat4_cast(rot);
            }
        }
        else if (selection.GetSelectionType() == SelectionType::Camera)
        {
            int index = selection.GetSelectedNodeIndex();
            Camera *camera = GetGlobalSystem<RendererSystem>()->GetScene().GetCamera(index);
            if (!camera)
                return;

            matrix = translate(mat4(1.0f), camera->GetPosition()) * mat4_cast(quat(camera->GetEuler()));
        }
        else
        {
            return;
        }

        if (ImGuizmo::Manipulate(value_ptr(view), value_ptr(proj), op, mode, value_ptr(matrix)))
        {
            vec3 pos, scale, skew;
            quat rot;
            vec4 persp;
            decompose(matrix, scale, rot, pos, skew, persp);

            if (useLight)
            {
                // Guard against NaN
                if (glm::any(glm::isnan(pos)) || glm::any(glm::isinf(pos)))
                    return;
                if (glm::any(glm::isnan(rot)) || glm::any(glm::isinf(rot)))
                    return;

                LightSystem *ls = GetGlobalSystem<LightSystem>();

                if (lightType == LightType::Point)
                {
                    ls->GetPointLights()[lightIndex].position = vec4(pos, ls->GetPointLights()[lightIndex].position.w);
                }
                else if (lightType == LightType::Spot)
                {
                    SpotLight &spot = ls->GetSpotLights()[lightIndex];
                    spot.position = vec4(pos, spot.position.w);
                    spot.rotation = vec4(rot.x, rot.y, rot.z, rot.w);
                }
                else if (lightType == LightType::Directional)
                {
                    if (!ls->GetDirectionalLights().empty())
                    {
                        // Update position
                        ls->GetDirectionalLights()[0].position = vec4(pos, 1.0f);

                        // Update direction
                        vec3 newDir = rot * vec3(0, 0, -1); // Assuming -Z is forward
                        if (!glm::any(glm::isnan(newDir)) && !glm::any(glm::isinf(newDir)))
                        {
                            ls->GetDirectionalLights()[0].direction = vec4(newDir, 0.0f);
                            
                            // Also update GlobalSettings so it persists/propagates
                            auto& gSettings = Settings::Get<GlobalSettings>();
                            gSettings.sun_direction[0] = newDir.x;
                            gSettings.sun_direction[1] = newDir.y;
                            gSettings.sun_direction[2] = newDir.z;
                        }
                    }
                }
                else if (lightType == LightType::Area)
                {
                    AreaLight &area = ls->GetAreaLights()[lightIndex];
                    area.position = vec4(pos, area.position.w);
                    area.rotation = vec4(rot.x, rot.y, rot.z, rot.w);
                }
            }
            else if (selection.GetSelectionType() == SelectionType::Camera)
            {
                int index = selection.GetSelectedNodeIndex();
                Camera *camera = GetGlobalSystem<RendererSystem>()->GetScene().GetCamera(index);
                if (camera)
                {
                    camera->SetPosition(pos);
                    vec3 euler = glm::eulerAngles(rot);
                    camera->SetEuler(euler);
                }
            }
            else
            {
                ApplyTransformToNode(selection.GetSelectedNodeInfo(), matrix);
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

        // Guard against NaN
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                if (std::isnan(newWorldMatrix[i][j]) || std::isinf(newWorldMatrix[i][j]))
                    return;

        int selectedNodeIndex = selection.GetSelectedNodeIndex();
        auto &nodeInfos = model->GetNodeInfos();

        int parentIdx = nodeInfo->parent;
        mat4 parentWorldMatrix = (parentIdx >= 0) ? nodeInfos[parentIdx].ubo.worldMatrix : model->GetMatrix();

        // Guard against singular matrix (zero scale)
        float det = glm::determinant(parentWorldMatrix);
        if (std::abs(det) < 1e-6f)
            return;

        nodeInfo->localMatrix = inverse(parentWorldMatrix) * newWorldMatrix;

        model->MarkDirty(selectedNodeIndex);
    }

    void DrawRing(const vec3 &center, float radius, const vec3 &axis, const mat4 &viewProj, const ImVec2 &viewMin, const ImVec2 &viewSize, ImU32 color, int segments = 32)
    {
        ImDrawList *drawList = ImGui::GetWindowDrawList();

        vec3 tangent;
        if (glm::abs(axis.y) < 0.99f)
            tangent = glm::normalize(glm::cross(axis, vec3(0, 1, 0)));
        else
            tangent = glm::normalize(glm::cross(axis, vec3(1, 0, 0)));

        vec3 bitangent = glm::normalize(glm::cross(axis, tangent));

        ImVec2 prevPoint;
        for (int i = 0; i <= segments; i++)
        {
            float angle = (float)i / (float)segments * 2.0f * 3.14159f;
            vec3 pos = center + (tangent * cos(angle) + bitangent * sin(angle)) * radius;

            vec4 clipPos = viewProj * vec4(pos, 1.0f);
            if (clipPos.w <= 0)
                continue;

            vec2 ndcPos = vec2(clipPos) / clipPos.w;
            ImVec2 screenPos;
            screenPos.x = (ndcPos.x * 0.5f + 0.5f) * viewSize.x + viewMin.x;
            screenPos.y = (ndcPos.y * 0.5f + 0.5f) * viewSize.y + viewMin.y;

            if (i > 0)
                drawList->AddLine(prevPoint, screenPos, color, 2.0f);

            prevPoint = screenPos;
        }
    }

    vec3 GetSpotDirection(const vec4 &rotation)
    {
        quat q = quat(rotation.w, rotation.x, rotation.y, rotation.z);
        vec3 dir = q * vec3(0, 0, -1);

        return glm::normalize(dir);
    }

    void SceneView::DrawGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize)
    {
        if (GUIState::s_useTransformGizmo)
            DrawTransformGizmo(imageMin, imageSize);

        if (GUIState::s_useLightGizmos)
            DrawLightGizmos(imageMin, imageSize);

        DrawCameraGizmos(imageMin, imageSize);
    }

    bool SceneView::DrawGizmoIcon(const vec3 &pos, const char *icon, const mat4 &viewProj, const ImVec2 &imageMin, const ImVec2 &imageSize, bool isSelected, const char *id)
    {
        vec4 clipPos = viewProj * vec4(pos, 1.0f);
        if (clipPos.w <= 0.0f)
            return false;

        vec2 ndcPos = vec2(clipPos) / clipPos.w;
        if (!isSelected && (ndcPos.x < -1.0f || ndcPos.x > 1.0f || ndcPos.y < -1.0f || ndcPos.y > 1.0f))
            return false;

        float x = (ndcPos.x * 0.5f + 0.5f) * imageSize.x + imageMin.x;
        float y = (ndcPos.y * 0.5f + 0.5f) * imageSize.y + imageMin.y;

        float scale = isSelected ? 2.0f : 1.0f;
        ImVec2 textSize = ImGui::CalcTextSize(icon);
        textSize.x *= scale;
        textSize.y *= scale;

        // Clamp to window bounds to avoid scrollbars
        x = glm::clamp(x, imageMin.x + textSize.x * 0.5f, imageMin.x + imageSize.x - textSize.x * 0.5f);
        y = glm::clamp(y, imageMin.y + textSize.y * 0.5f, imageMin.y + imageSize.y - textSize.y * 0.5f);

        ImVec2 iconPos(x - textSize.x * 0.5f, y - textSize.y * 0.5f);

        ImGui::SetCursorScreenPos(iconPos);
        ImGui::SetWindowFontScale(scale);
        ImGui::PushStyleColor(ImGuiCol_Text, isSelected ? ImVec4(1, 1, 0, 1) : ImVec4(1, 1, 1, 1));
        ImGui::Text("%s", icon);
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.0f);

        ImGui::SetCursorScreenPos(iconPos);
        return ImGui::InvisibleButton(id, textSize);
    }

    void SceneView::DrawLightGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize)
    {
        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        Scene &scene = renderer->GetScene();
        Camera *camera = scene.GetActiveCamera();

        if (!camera)
            return;

        LightSystem *ls = GetGlobalSystem<LightSystem>();

        mat4 view = camera->GetView();
        mat4 proj = camera->GetProjectionNoJitter();
        mat4 viewProj = proj * view;

        auto drawIcon = [&](const vec3 &pos, const char *icon, LightType type, int index)
        {
            // Color based on selection
            bool isSelected = SelectionManager::Instance().GetSelectedLightType() == type &&
                              SelectionManager::Instance().GetSelectedLightIndex() == index &&
                              SelectionManager::Instance().GetSelectionType() == SelectionType::Light;

            if (isSelected)
            {
                ImU32 gizmoColor = IM_COL32(255, 255, 0, 255);
                if (type == LightType::Point)
                {
                    float radius = ls->GetPointLights()[index].position.w;
                    if (radius > 0.0f)
                    {
                        DrawRing(pos, radius, vec3(1, 0, 0), viewProj, imageMin, imageSize, gizmoColor);
                        DrawRing(pos, radius, vec3(0, 1, 0), viewProj, imageMin, imageSize, gizmoColor);
                        DrawRing(pos, radius, vec3(0, 0, 1), viewProj, imageMin, imageSize, gizmoColor);
                    }
                }
                else if (type == LightType::Spot)
                {
                    float range = ls->GetSpotLights()[index].position.w;
                    float innerAngle = ls->GetSpotLights()[index].params.x;
                    float falloff = ls->GetSpotLights()[index].params.y;
                    float outerAngle = innerAngle + falloff;

                    vec3 dir = GetSpotDirection(ls->GetSpotLights()[index].rotation);
                    vec3 baseCenter = pos + dir * range;

                    float innerRadius = range * tan(glm::radians(innerAngle));
                    DrawRing(baseCenter, innerRadius, dir, viewProj, imageMin, imageSize, gizmoColor);

                    float outerRadius = range * tan(glm::radians(outerAngle));
                    DrawRing(baseCenter, outerRadius, dir, viewProj, imageMin, imageSize, gizmoColor);

                    // Draw lines from tip to base (Inner)
                    ImDrawList *drawList = ImGui::GetWindowDrawList();
                    vec3 tangent = glm::normalize(glm::cross(dir, vec3(0, 1, 0)));
                    if (glm::length(tangent) < 0.001f)
                        tangent = glm::normalize(glm::cross(dir, vec3(1, 0, 0)));
                    vec3 bitangent = glm::cross(dir, tangent);

                    // Draw lines for Outer Cone to show the full volume?
                    // Usually lines go to the outer extent.

                    vec3 pts[4] = {
                        baseCenter + tangent * outerRadius,
                        baseCenter - tangent * outerRadius,
                        baseCenter + bitangent * outerRadius,
                        baseCenter - bitangent * outerRadius};

                    for (int k = 0; k < 4; k++)
                    {
                        vec4 clipP1 = viewProj * vec4(pos, 1.0f);
                        vec4 clipP2 = viewProj * vec4(pts[k], 1.0f);

                        if (clipP1.w > 0 && clipP2.w > 0)
                        {
                            vec2 ndcP1 = vec2(clipP1) / clipP1.w;
                            vec2 ndcP2 = vec2(clipP2) / clipP2.w;

                            ImVec2 sP1, sP2;
                            sP1.x = (ndcP1.x * 0.5f + 0.5f) * imageSize.x + imageMin.x;
                            sP1.y = (ndcP1.y * 0.5f + 0.5f) * imageSize.y + imageMin.y;
                            sP2.x = (ndcP2.x * 0.5f + 0.5f) * imageSize.x + imageMin.x;
                            sP2.y = (ndcP2.y * 0.5f + 0.5f) * imageSize.y + imageMin.y;

                            drawList->AddLine(sP1, sP2, gizmoColor, 2.0f);
                        }
                    }

                    // Draw central direction line
                    vec4 clipP1 = viewProj * vec4(pos, 1.0f);
                    vec4 clipP2 = viewProj * vec4(baseCenter, 1.0f);
                    if (clipP1.w > 0 && clipP2.w > 0)
                    {
                        vec2 ndcP1 = vec2(clipP1) / clipP1.w;
                        vec2 ndcP2 = vec2(clipP2) / clipP2.w;
                        ImVec2 sP1, sP2;
                        sP1.x = (ndcP1.x * 0.5f + 0.5f) * imageSize.x + imageMin.x;
                        sP1.y = (ndcP1.y * 0.5f + 0.5f) * imageSize.y + imageMin.y;
                        sP2.x = (ndcP2.x * 0.5f + 0.5f) * imageSize.x + imageMin.x;
                        sP2.y = (ndcP2.y * 0.5f + 0.5f) * imageSize.y + imageMin.y;

                        drawList->AddLine(sP1, sP2, gizmoColor, 1.0f);
                    }
                }
                else if (type == LightType::Area)
                {
                    float width = ls->GetAreaLights()[index].size.x;
                    float height = ls->GetAreaLights()[index].size.y;
                    vec3 dir = GetDirectionFromRotation(ls->GetAreaLights()[index].rotation);

                    vec3 right = glm::normalize(glm::cross(dir, vec3(0, 1, 0)));
                    if (glm::length(right) < 0.001f)
                        right = glm::normalize(glm::cross(dir, vec3(1, 0, 0)));
                    vec3 up = glm::normalize(glm::cross(right, dir));

                    vec3 center = pos;
                    vec3 p1 = center + (right * width * 0.5f) + (up * height * 0.5f);
                    vec3 p2 = center - (right * width * 0.5f) + (up * height * 0.5f);
                    vec3 p3 = center - (right * width * 0.5f) - (up * height * 0.5f);
                    vec3 p4 = center + (right * width * 0.5f) - (up * height * 0.5f);

                    // Draw Rectangle
                    vec3 pts[4] = {p1, p2, p3, p4};
                    ImDrawList *drawList = ImGui::GetWindowDrawList();

                    for (int k = 0; k < 4; k++)
                    {
                        vec3 pA = pts[k];
                        vec3 pB = pts[(k + 1) % 4];

                        vec4 clipP1 = viewProj * vec4(pA, 1.0f);
                        vec4 clipP2 = viewProj * vec4(pB, 1.0f);

                        if (clipP1.w > 0 && clipP2.w > 0)
                        {
                            vec2 ndcP1 = vec2(clipP1) / clipP1.w;
                            vec2 ndcP2 = vec2(clipP2) / clipP2.w;

                            ImVec2 sP1, sP2;
                            sP1.x = (ndcP1.x * 0.5f + 0.5f) * imageSize.x + imageMin.x;
                            sP1.y = (ndcP1.y * 0.5f + 0.5f) * imageSize.y + imageMin.y;
                            sP2.x = (ndcP2.x * 0.5f + 0.5f) * imageSize.x + imageMin.x;
                            sP2.y = (ndcP2.y * 0.5f + 0.5f) * imageSize.y + imageMin.y;

                            drawList->AddLine(sP1, sP2, gizmoColor, 2.0f);
                        }
                    }

                    // Direction Line
                    float range = ls->GetAreaLights()[index].position.w;
                    vec3 end = center + dir * range;
                    vec4 clipP1 = viewProj * vec4(center, 1.0f);
                    vec4 clipP2 = viewProj * vec4(end, 1.0f);

                    if (clipP1.w > 0 && clipP2.w > 0)
                    {
                        vec2 ndcP1 = vec2(clipP1) / clipP1.w;
                        vec2 ndcP2 = vec2(clipP2) / clipP2.w;
                        ImVec2 sP1, sP2;
                        sP1.x = (ndcP1.x * 0.5f + 0.5f) * imageSize.x + imageMin.x;
                        sP1.y = (ndcP1.y * 0.5f + 0.5f) * imageSize.y + imageMin.y;
                        sP2.x = (ndcP2.x * 0.5f + 0.5f) * imageSize.x + imageMin.x;
                        sP2.y = (ndcP2.y * 0.5f + 0.5f) * imageSize.y + imageMin.y;

                        drawList->AddLine(sP1, sP2, gizmoColor, 1.0f);
                    }
                }
                else if (type == LightType::Directional)
                {
                    vec3 dir = vec3(ls->GetDirectionalLights()[index].direction);
                    vec3 center = pos;
                    vec3 end = center + dir * 2.0f; // Fixed length for directional light visualization

                    ImDrawList *drawList = ImGui::GetWindowDrawList();
                    
                    // Draw a line representing direction
                    vec4 clipP1 = viewProj * vec4(center, 1.0f);
                    vec4 clipP2 = viewProj * vec4(end, 1.0f);

                    if (clipP1.w > 0 && clipP2.w > 0)
                    {
                        vec2 ndcP1 = vec2(clipP1) / clipP1.w;
                        vec2 ndcP2 = vec2(clipP2) / clipP2.w;
                        ImVec2 sP1, sP2;
                        sP1.x = (ndcP1.x * 0.5f + 0.5f) * imageSize.x + imageMin.x;
                        sP1.y = (ndcP1.y * 0.5f + 0.5f) * imageSize.y + imageMin.y;
                        sP2.x = (ndcP2.x * 0.5f + 0.5f) * imageSize.x + imageMin.x;
                        sP2.y = (ndcP2.y * 0.5f + 0.5f) * imageSize.y + imageMin.y;

                        drawList->AddLine(sP1, sP2, gizmoColor, 2.0f);
                        
                        // Draw arrow head? Optional.
                    }
                    
                    // Draw a ring to simulate the "sun" disk
                    DrawRing(center, 0.5f, dir, viewProj, imageMin, imageSize, gizmoColor);
                }
            }

            std::string id = "##LightIcon" + std::to_string((int)type) + "_" + std::to_string(index);
            if (DrawGizmoIcon(pos, icon, viewProj, imageMin, imageSize, isSelected, id.c_str()))
            {
                SelectionManager::Instance().Select(type, index);
            }
        };

        // Point Lights
        for (int i = 0; i < (int)ls->GetPointLights().size(); i++)
        {
            drawIcon(vec3(ls->GetPointLights()[i].position), ICON_FA_LIGHTBULB, LightType::Point, i);
        }

        // Spot Lights
        for (int i = 0; i < (int)ls->GetSpotLights().size(); i++)
        {
            drawIcon(vec3(ls->GetSpotLights()[i].position), ICON_FA_LIGHTBULB, LightType::Spot, i);
        }

        // Directional Lights
        for (int i = 0; i < (int)ls->GetDirectionalLights().size(); i++)
        {
             drawIcon(vec3(ls->GetDirectionalLights()[i].position), ICON_FA_SUN, LightType::Directional, i);
        }

        // Area Lights
        for (int i = 0; i < (int)ls->GetAreaLights().size(); i++)
        {
            drawIcon(vec3(ls->GetAreaLights()[i].position), ICON_FA_LIGHTBULB, LightType::Area, i);
        }
    }
    void SceneView::DrawCameraGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize)
    {
        if (!GUIState::s_useCameraGizmos)
            return;

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        Scene &scene = renderer->GetScene();
        Camera *activeCamera = scene.GetActiveCamera();
        if (!activeCamera)
            return;

        mat4 view = activeCamera->GetView();
        mat4 proj = activeCamera->GetProjectionNoJitter();
        mat4 viewProj = proj * view;

        auto &cameras = scene.GetCameras();
        auto &selection = SelectionManager::Instance();

        for (int i = 0; i < (int)cameras.size(); i++)
        {
            if (i == 0)
                continue; // Hide gizmo for the main active camera

            Camera *camera = cameras[i];
            // if (camera == activeCamera) continue; // Should we draw active camera icon? User said "icon in viewport"

            vec3 pos = camera->GetPosition();
            bool isSelected = selection.GetSelectionType() == SelectionType::Camera &&
                              selection.GetSelectedNodeIndex() == i;

            std::string iconId = "##CameraIcon" + std::to_string(i);
            if (DrawGizmoIcon(pos, ICON_FA_VIDEO, viewProj, imageMin, imageSize, isSelected, iconId.c_str()))
            {
                selection.Select(nullptr, i, SelectionType::Camera);
            }

            if (isSelected)
            {
                // Draw Frustum
                mat4 invCamVP = camera->GetInvViewProjection();
                // If camera uses infinite far plane, we use a finite far plane for visualization
                mat4 visualProj = camera->GetProjectionNoJitter();
                // The standard projection has far at 0 for infinite reverse-z.
                // Let's just use the inverse view projection and clamp NDC Z.
                // Actually, let's use a fixed far distance like 10.0 for visualization if it's infinite.

                bool isInfinite = camera->GetFarPlane() >= 3.402823466e+38F; // FLT_MAX
                float nearPlane = camera->GetNearPlane();
                float farZ = isInfinite ? 0.00001f : (nearPlane / camera->GetFarPlane());

                vec4 frustumCornersNDC[8] = {
                    vec4(-1, -1, 1, 1), vec4(1, -1, 1, 1), vec4(1, 1, 1, 1), vec4(-1, 1, 1, 1),            // Near (z=1 in reverse-z)
                    vec4(-1, -1, farZ, 1), vec4(1, -1, farZ, 1), vec4(1, 1, farZ, 1), vec4(-1, 1, farZ, 1) // "Far"
                };

                ImVec2 cornersScreen[8];
                bool valid[8];
                for (int j = 0; j < 8; j++)
                {
                    vec4 worldPos = invCamVP * frustumCornersNDC[j];
                    worldPos /= worldPos.w;

                    vec4 activeClipPos = viewProj * vec4(vec3(worldPos), 1.0f);
                    valid[j] = activeClipPos.w > 0.0f;
                    if (valid[j])
                    {
                        vec2 activeNdc = vec2(activeClipPos) / activeClipPos.w;
                        cornersScreen[j].x = (activeNdc.x * 0.5f + 0.5f) * imageSize.x + imageMin.x;
                        cornersScreen[j].y = (activeNdc.y * 0.5f + 0.5f) * imageSize.y + imageMin.y;
                    }
                }

                ImDrawList *drawList = ImGui::GetWindowDrawList();
                ImU32 frustumColor = IM_COL32(255, 255, 0, 255);

                auto drawClippedLine = [&](int a, int b)
                {
                    if (valid[a] && valid[b])
                        drawList->AddLine(cornersScreen[a], cornersScreen[b], frustumColor, 1.0f);
                };

                for (int j = 0; j < 4; j++)
                {
                    drawClippedLine(j, (j + 1) % 4); // Near Plane
                    if (!isInfinite)
                        drawClippedLine(j + 4, ((j + 1) % 4) + 4); // Far Plane
                    drawClippedLine(j, j + 4);                     // Connections
                }
            }
        }
    }
} // namespace pe
