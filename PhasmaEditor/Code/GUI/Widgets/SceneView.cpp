#include "SceneView.h"
#include "API/Image.h"
#include "Base/ThreadPool.h"
#include "Camera/Camera.h"
#include "GUI/GUI.h"
#include "GUI/GUIState.h"
#include "GUI/IconsFontAwesome.h"
#include "Particles/ParticleManager.h"
#include "Scene/ModelAsset.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Scene/SelectionManager.h"
#include "Systems/RendererSystem.h"
#include "imgui/ImGuizmo.h"
#include "imgui/imgui_internal.h"

namespace pe
{
    static void HandleViewportDocking(GUI *gui)
    {
        if (GUIState::s_sceneViewFloating)
        {
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
            return;
        }

        if (GUIState::s_sceneViewRedockQueued && gui->GetDockspaceId() != 0)
        {
            ImGui::DockBuilderDockWindow("Viewport", gui->GetDockspaceId());
            GUIState::s_sceneViewRedockQueued = false;
        }
    }

    static bool BeginViewportWindow(bool floating)
    {
        ImGuiWindowFlags flags = 0;
        if (floating)
            flags |= ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const bool open = ImGui::Begin("Viewport", nullptr, flags);
        ImGui::PopStyleVar();

        GUIState::s_sceneViewFocused = ImGui::IsWindowFocused();
        return open;
    }

    static void DestroyViewportTextureId(GUI *gui)
    {
        if (!GUIState::s_viewportTextureId)
            return;

        if (gui)
            gui->ReleaseImageTexture(GUIState::s_viewportTextureId);
        else
            GUIState::s_viewportTextureId = nullptr;
    }

    static void RecreateSceneViewImage(GUI *gui, RendererSystem *renderer, Image *displayRT)
    {
        if (!displayRT)
            return;

        Image::Destroy(GUIState::s_sceneViewImage);
        GUIState::s_sceneViewImage = renderer->CreateFSSampledImage(false);

        DestroyViewportTextureId(gui);
    }

    static bool EnsureSceneViewImageMatchesDisplay(GUI *gui, RendererSystem *renderer, Image *displayRT)
    {
        if (!displayRT)
            return false;

        if (!GUIState::s_sceneViewImage)
        {
            RecreateSceneViewImage(gui, renderer, displayRT);
            return true;
        }

        if (GUIState::s_sceneViewImage->GetWidth() != displayRT->GetWidth() ||
            GUIState::s_sceneViewImage->GetHeight() != displayRT->GetHeight())
        {
            RecreateSceneViewImage(gui, renderer, displayRT);
            return true;
        }

        return true;
    }

    static Image *GetSceneTexture(Image *displayRT)
    {
        return GUIState::s_sceneViewImage ? GUIState::s_sceneViewImage : displayRT;
    }

    static bool EnsureViewportTextureId(GUI *gui, Image *sceneTexture)
    {
        if (!gui || !sceneTexture || !sceneTexture->HasSRV())
            return false;

        if (GUIState::s_viewportTextureId)
            return true;

        GUIState::s_viewportTextureId = gui->RegisterImageTexture(sceneTexture);
        return GUIState::s_viewportTextureId != nullptr;
    }

    static ImVec2 ComputeAspectFitSize(ImVec2 panel, float targetAspect)
    {
        if (panel.x <= 0.0f || panel.y <= 0.0f)
            return ImVec2(0, 0);

        const float panelAspect = panel.x / panel.y;

        ImVec2 size;
        if (panelAspect > targetAspect)
        {
            size.y = panel.y;
            size.x = size.y * targetAspect;
        }
        else
        {
            size.x = panel.x;
            size.y = size.x / targetAspect;
        }
        return size;
    }

    static void CenterCursorForImage(ImVec2 panel, ImVec2 imageSize)
    {
        ImVec2 cursor = ImGui::GetCursorPos();
        cursor.x += (panel.x - imageSize.x) * 0.5f;
        cursor.y += (panel.y - imageSize.y) * 0.5f;
        ImGui::SetCursorPos(cursor);
    }

    // -----------------------------
    // Gizmo Utils
    // -----------------------------
    namespace GizmoUtils
    {
        static glm::mat4 ZFlipMatrix()
        {
            static const glm::mat4 s = []()
            {
                glm::mat4 m(1.0f);
                m[2][2] = -1.0f;
                return m;
            }();
            return s;
        }

        static glm::mat4 FlipHandedness(const glm::mat4 &m)
        {
            const glm::mat4 &s = ZFlipMatrix();
            return s * m * s;
        }

        struct SelectionContext
        {
            glm::mat4 matrix = glm::mat4(1.0f);
            bool valid = false;
            bool useLight = false;
            LightType lightType = LightType::Point;
            int lightIndex = -1;
            int cameraIndex = -1;
            int emitterIndex = -1;
            NodeId *node = nullptr;
        };

        static SelectionContext GetSelectionContext()
        {
            SelectionContext ctx;
            auto &selection = SelectionManager::Instance();

            if (!selection.HasSelection())
                return ctx;

            if (selection.GetSelectionType() == SelectionType::Node)
            {
                ctx.node = selection.GetSelectedNode();
                if (ctx.node)
                {
                    Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();
                    if (!scene.IsNodeAlive(ctx.node))
                    {
                        selection.ClearSelection();
                        ctx.node = nullptr;
                        return ctx;
                    }
                    ctx.matrix = scene.GetWorldMatrix(ctx.node);
                    ctx.valid = true;
                }
            }
            else if (selection.GetSelectionType() == SelectionType::Light)
            {
                ctx.lightType = selection.GetSelectedLightType();
                ctx.lightIndex = selection.GetSelectedLightIndex();
                ctx.useLight = true;

                Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();
                if (ctx.lightType == LightType::Point && ctx.lightIndex >= 0 && ctx.lightIndex < (int)scene.GetPointLights().size())
                {
                    ctx.matrix = glm::translate(glm::mat4(1.0f), glm::vec3(scene.GetPointLights()[ctx.lightIndex].position));
                    ctx.valid = true;
                }
                else if (ctx.lightType == LightType::Spot && ctx.lightIndex >= 0 && ctx.lightIndex < (int)scene.GetSpotLights().size())
                {
                    const auto &spot = scene.GetSpotLights()[ctx.lightIndex];
                    glm::vec3 start = glm::vec3(spot.position);
                    glm::quat rot = glm::quat(spot.rotation.w, spot.rotation.x, spot.rotation.y, spot.rotation.z);
                    ctx.matrix = glm::translate(glm::mat4(1.0f), start) * glm::mat4_cast(rot);
                    ctx.valid = true;
                }
                else if (ctx.lightType == LightType::Directional && ctx.lightIndex >= 0 && ctx.lightIndex < (int)scene.GetDirectionalLights().size())
                {
                    const auto &dirLight = scene.GetDirectionalLights()[ctx.lightIndex];
                    glm::vec3 pos = glm::vec3(dirLight.position);
                    glm::quat rot = glm::quat(dirLight.rotation.w, dirLight.rotation.x, dirLight.rotation.y, dirLight.rotation.z);
                    ctx.matrix = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot);
                    ctx.valid = true;
                }
                else if (ctx.lightType == LightType::Area && ctx.lightIndex >= 0 && ctx.lightIndex < (int)scene.GetAreaLights().size())
                {
                    const auto &area = scene.GetAreaLights()[ctx.lightIndex];
                    glm::vec3 start = glm::vec3(area.position);
                    glm::quat rot = glm::quat(area.rotation.w, area.rotation.x, area.rotation.y, area.rotation.z);
                    ctx.matrix = glm::translate(glm::mat4(1.0f), start) * glm::mat4_cast(rot);
                    ctx.valid = true;
                }
            }
            else if (selection.GetSelectionType() == SelectionType::Camera)
            {
                ctx.cameraIndex = selection.GetSelectedCameraIndex();
                auto cameras = GetGlobalSystem<RendererSystem>()->GetScene().GetCameras();
                if (ctx.cameraIndex >= 0 && ctx.cameraIndex < (int)cameras.size())
                {
                    Camera *c = cameras[ctx.cameraIndex];
                    glm::vec3 pos = c->GetPosition();
                    glm::quat rot = glm::quat(c->GetEuler());
                    ctx.matrix = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot);
                    ctx.valid = true;
                }
            }
            else if (selection.GetSelectionType() == SelectionType::Emitter)
            {
                ctx.emitterIndex = selection.GetSelectedEmitterIndex();
                ParticleManager *pm = GetGlobalSystem<RendererSystem>()->GetScene().GetParticleManager();
                if (pm)
                {
                    auto &emitters = pm->GetEmitters();
                    if (ctx.emitterIndex >= 0 && ctx.emitterIndex < (int)emitters.size())
                    {
                        glm::vec3 pos = glm::vec3(emitters[ctx.emitterIndex].position);
                        glm::vec3 vel = glm::vec3(emitters[ctx.emitterIndex].velocity);
                        float speed = glm::length(vel);

                        glm::quat rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                        if (speed > 1e-6f)
                        {
                            glm::vec3 dir = vel / speed;
                            // Rotation from +Y to velocity direction
                            glm::vec3 up(0.0f, 1.0f, 0.0f);
                            float d = glm::dot(up, dir);
                            if (d < -0.9999f)
                                rot = glm::angleAxis(glm::pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f));
                            else if (d < 0.9999f)
                                rot = glm::rotation(up, dir);
                        }

                        ctx.matrix = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot);
                        ctx.valid = true;
                    }
                }
            }
            return ctx;
        }

        static void DrawRing(const glm::vec3 &center, float radius, const glm::vec3 &axis,
                             const glm::mat4 &viewProj, const ImVec2 &viewMin, const ImVec2 &viewSize,
                             ImU32 color, int segments = 32)
        {
            ImDrawList *drawList = ImGui::GetWindowDrawList();

            glm::vec3 tangent;
            if (glm::abs(axis.y) < 0.99f)
                tangent = glm::normalize(glm::cross(axis, glm::vec3(0, 1, 0)));
            else
                tangent = glm::normalize(glm::cross(axis, glm::vec3(1, 0, 0)));

            glm::vec3 bitangent = glm::normalize(glm::cross(axis, tangent));

            bool hasPrev = false;
            ImVec2 prevPoint = ImVec2(0, 0);

            for (int i = 0; i <= segments; i++)
            {
                float angle = (float)i / (float)segments * 2.0f * 3.14159265f;
                const glm::vec3 pos = center + (tangent * cos(angle) + bitangent * sin(angle)) * radius;

                const glm::vec4 clipPos = viewProj * glm::vec4(pos, 1.0f);

                if (clipPos.w <= 1e-6f)
                {
                    hasPrev = false;
                    continue;
                }

                const glm::vec2 ndcPos = glm::vec2(clipPos) / clipPos.w;

                if (ndcPos.x < -2.0f || ndcPos.x > 2.0f || ndcPos.y < -2.0f || ndcPos.y > 2.0f)
                {
                    hasPrev = false;
                    continue;
                }

                ImVec2 screenPos;
                screenPos.x = (ndcPos.x * 0.5f + 0.5f) * viewSize.x + viewMin.x;
                screenPos.y = (ndcPos.y * 0.5f + 0.5f) * viewSize.y + viewMin.y;

                if (hasPrev)
                    drawList->AddLine(prevPoint, screenPos, color, 2.0f);

                prevPoint = screenPos;
                hasPrev = true;
            }
        }

        static void ApplySelectionContext(const SelectionContext &ctx, const glm::mat4 &newMatrix)
        {
            if (!ctx.valid)
                return;

            // Decompose logic
            glm::vec3 pos, scale, skew;
            glm::quat rot;
            glm::vec4 persp;
            glm::decompose(newMatrix, scale, rot, pos, skew, persp);

            // NAN Check
            if (glm::any(glm::isnan(pos)) || glm::any(glm::isinf(pos)) ||
                glm::any(glm::isnan(rot)) || glm::any(glm::isinf(rot)))
                return;

            if (ctx.useLight)
            {
                Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();

                if (ctx.lightType == LightType::Point)
                {
                    scene.GetPointLights()[ctx.lightIndex].position = glm::vec4(pos, scene.GetPointLights()[ctx.lightIndex].position.w);
                }
                else if (ctx.lightType == LightType::Spot)
                {
                    auto &spot = scene.GetSpotLights()[ctx.lightIndex];
                    spot.position = glm::vec4(pos, spot.position.w);
                    spot.rotation = glm::vec4(rot.x, rot.y, rot.z, rot.w);
                }
                else if (ctx.lightType == LightType::Directional && ctx.lightIndex >= 0 && ctx.lightIndex < (int)scene.GetDirectionalLights().size())
                {
                    auto &dirLight = scene.GetDirectionalLights()[ctx.lightIndex];
                    dirLight.position = glm::vec4(pos, dirLight.position.w);
                    dirLight.rotation = glm::vec4(rot.x, rot.y, rot.z, rot.w);
                }
                else if (ctx.lightType == LightType::Area)
                {
                    auto &area = scene.GetAreaLights()[ctx.lightIndex];
                    area.position = glm::vec4(pos, area.position.w);
                    area.rotation = glm::vec4(rot.x, rot.y, rot.z, rot.w);
                }
            }
            else if (ctx.cameraIndex != -1)
            {
                auto cameras = GetGlobalSystem<RendererSystem>()->GetScene().GetCameras();
                if (ctx.cameraIndex >= 0 && ctx.cameraIndex < (int)cameras.size())
                {
                    auto &selection = SelectionManager::Instance();
                    if (selection.GetGizmoOperation() == GizmoOperation::Translate)
                        cameras[ctx.cameraIndex]->SetPosition(pos);
                    else if (selection.GetGizmoOperation() == GizmoOperation::Rotate)
                    {
                        float translation[3], rotation[3], scale[3];
                        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(newMatrix), translation, rotation, scale);
                        cameras[ctx.cameraIndex]->SetEuler(glm::radians(glm::vec3(rotation[0], rotation[1], rotation[2])));
                    }
                }
            }
            else if (ctx.emitterIndex != -1)
            {
                ParticleManager *pm = GetGlobalSystem<RendererSystem>()->GetScene().GetParticleManager();
                if (pm)
                {
                    auto &emitters = pm->GetEmitters();
                    if (ctx.emitterIndex >= 0 && ctx.emitterIndex < (int)emitters.size())
                    {
                        auto &emitter = emitters[ctx.emitterIndex];
                        emitter.position = glm::vec4(pos, emitter.position.w);

                        // Rotate velocity direction: new direction = rot * +Y, preserve speed
                        float speed = glm::length(glm::vec3(emitter.velocity));
                        if (speed > 1e-6f)
                        {
                            glm::vec3 newDir = rot * glm::vec3(0.0f, 1.0f, 0.0f);
                            emitter.velocity = glm::vec4(newDir * speed, emitter.velocity.w);
                        }

                        pm->UpdateEmitterBuffer();
                    }
                }
            }
            else if (ctx.node)
            {
                // Guard against NaN
                for (int i = 0; i < 4; ++i)
                    for (int j = 0; j < 4; ++j)
                        if (std::isnan(newMatrix[i][j]) || std::isinf(newMatrix[i][j]))
                            return;

                Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();
                NodeId *parent = scene.GetParent(ctx.node);
                glm::mat4 parentWorldMatrix = parent ? scene.GetWorldMatrix(parent) : glm::mat4(1.0f);

                // Guard against singular matrix
                float det = glm::determinant(parentWorldMatrix);
                if (std::abs(det) < 1e-6f)
                    return;

                mat4 localMatrix = glm::inverse(parentWorldMatrix) * newMatrix;
                scene.SetLocalMatrix(ctx.node, localMatrix);

                // If this node is a camera, sync position/euler so Scene::Update doesn't overwrite
                if (scene.GetComponentFlags(ctx.node) & Component_Camera)
                {
                    Camera *cam = scene.GetCameraForNode(ctx.node);
                    if (cam)
                    {
                        cam->SetPosition(vec3(localMatrix[3]));
                        cam->SetEuler(glm::eulerAngles(quat_cast(mat3(localMatrix))));
                    }
                }
            }
        }
    } // namespace GizmoUtils

    // -----------------------------
    // SceneView
    // -----------------------------
    void SceneView::Init(GUI *gui)
    {
        Widget::Init(gui);
    }

    void SceneView::Update()
    {
        HandleViewportDocking(m_gui);

        if (!BeginViewportWindow(GUIState::s_sceneViewFloating))
        {
            ImGui::End();
            return;
        }

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        Image *displayRT = renderer ? renderer->GetDisplayRT() : nullptr;

        if (!EnsureSceneViewImageMatchesDisplay(m_gui, renderer, displayRT))
        {
            ImGui::TextDisabled("Initializing viewport...");
            ImGui::End();
            return;
        }

        Image *sceneTexture = GetSceneTexture(displayRT);
        if (!sceneTexture)
        {
            ImGui::TextDisabled("Initializing viewport...");
            ImGui::End();
            return;
        }

        if (!EnsureViewportTextureId(m_gui, sceneTexture))
        {
            ImGui::TextDisabled("Initializing viewport...");
            ImGui::End();
            return;
        }

        // Draw image (aspect fit + centered)
        const ImVec2 panel = ImGui::GetContentRegionAvail();
        if (panel.x <= 0.0f || panel.y <= 0.0f)
        {
            ImGui::End();
            return;
        }

        const float targetAspect = (float)sceneTexture->GetWidth() / (float)sceneTexture->GetHeight();
        const ImVec2 imageSize = ComputeAspectFitSize(panel, targetAspect);

        CenterCursorForImage(panel, imageSize);
        const ImVec2 uv0(0.0f, 0.0f);
        const ImVec2 uv1(1.0f, 1.0f);
        ImGui::Image((ImTextureID)(intptr_t)GUIState::s_viewportTextureId, imageSize, uv0, uv1);

        const ImVec2 imageMin = ImGui::GetItemRectMin();
        const ImVec2 imageMax = ImGui::GetItemRectMax();
        const bool imageHovered = ImGui::IsItemHovered();

        // Gizmos (hit tests are in ImGuizmo)
        DrawGizmos(imageMin, imageSize);

        const bool overGizmo = ImGuizmo::IsOver();
        const bool usingGizmo = ImGuizmo::IsUsing();

        // Input (picking / focus) only when not interacting with gizmos
        if (!overGizmo && !usingGizmo)
        {
            if (imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                const ImVec2 mouse = ImGui::GetMousePos();
                const float nx = (mouse.x - imageMin.x) / (imageMax.x - imageMin.x);
                const float ny = (mouse.y - imageMin.y) / (imageMax.y - imageMin.y);
                PerformObjectPicking(nx, ny);
            }

            if (imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                ImGui::SetWindowFocus();
            }
        }

        // Drag&drop
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                const char *pathStr = (const char *)payload->Data;
                std::filesystem::path path(pathStr);

                std::string ext = path.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                const char *modelExts[] = {".gltf", ".glb", ".obj", ".fbx"};
                const bool isModel = std::any_of(std::begin(modelExts), std::end(modelExts),
                                                 [&](const char *e)
                                                 { return ext == e; });

                if (isModel && !GUIState::s_modelLoading)
                {
                    ThreadPool::GUI.Enqueue([path]()
                                            {
                        GUIState::s_modelLoading = true;
                        try
                        {
                            if (ModelAsset *m = ModelAsset::Load(path))
                                EventSystem::PushEvent(EventType::ModelLoaded, m);
                        }
                        catch (const std::exception &e)
                        {
                            PE_WARN("[Scene] Failed to load model: %s", e.what());
                        }
                        GUIState::s_modelLoading = false; });
                }
            }
            ImGui::EndDragDropTarget();
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
            NodeId *node;
        };
        std::vector<Intersection> intersections;

        uint32_t nodeCount = scene.GetNodeCount();
        for (uint32_t i = 0; i < nodeCount; i++)
        {
            NodeId *node = scene.GetNodeId(i);
            if (scene.GetMeshRef(node) < 0)
                continue;

            const AABB &aabb = scene.GetWorldAABB(node);

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
                intersections.push_back({t, node});
            }
        }

        auto &selection = SelectionManager::Instance();

        if (intersections.empty())
            return;

        // Sort by distance
        std::sort(intersections.begin(), intersections.end(), [](const Intersection &a, const Intersection &b)
                  { return a.t < b.t; });

        int selectIndex = 0;

        // Cyclic selection logic
        if (selection.HasSelection() && selection.GetSelectionType() == SelectionType::Node)
        {
            NodeId *currentNode = selection.GetSelectedNode();

            for (int i = 0; i < static_cast<int>(intersections.size()); i++)
            {
                if (intersections[i].node == currentNode)
                {
                    selectIndex = (i + 1) % intersections.size();
                    break;
                }
            }
        }

        selection.Select(intersections[selectIndex].node);
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
        Camera *cam = renderer->GetScene().GetActiveCamera();
        if (!cam)
            return;

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(imageMin.x, imageMin.y, imageSize.x, imageSize.y);

        // Disable interaction if Right Mouse Button is held down inside this window
        const bool isRightClick = ImGui::IsMouseDown(ImGuiMouseButton_Right) && ImGui::IsWindowFocused();
        ImGuizmo::Enable(!isRightClick);

        // --- Build matrices for ImGuizmo in RH space ---
        // Engine is LH, so convert view to RH.
        glm::mat4 viewLH = cam->GetView();
        glm::mat4 viewRH = GizmoUtils::FlipHandedness(viewLH);

        float fovY = cam->Fovy();
        float aspect = cam->GetAspect();
        float nearPlane = glm::max(0.001f, cam->GetNearPlane());
        float farPlane = 1000.0f;
        glm::mat4 projRH = glm::perspectiveRH_NO(fovY, aspect, nearPlane, farPlane);

        // Match Vulkan viewport Y direction so camera pitch maps correctly
        projRH[1][1] *= -1.0f;

        // Operation
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

        // Emitters support translate and rotate (no scale)
        if (selection.GetSelectionType() == SelectionType::Emitter && op == ImGuizmo::SCALE)
            op = ImGuizmo::TRANSLATE;

        ImGuizmo::MODE mode = ImGuizmo::WORLD;

        // selection context
        GizmoUtils::SelectionContext ctx = GizmoUtils::GetSelectionContext();
        if (!ctx.valid)
            return;

        glm::mat4 matrixLH = ctx.matrix;
        glm::mat4 matrixRH = GizmoUtils::FlipHandedness(matrixLH);

        // --- Manipulate in RH space ---
        if (ImGuizmo::Manipulate(glm::value_ptr(viewRH), glm::value_ptr(projRH), op, mode, glm::value_ptr(matrixRH)))
        {
            // Convert back to LH
            matrixLH = GizmoUtils::FlipHandedness(matrixRH);
            GizmoUtils::ApplySelectionContext(ctx, matrixLH);
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
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 imageMax(imageMin.x + imageSize.x, imageMin.y + imageSize.y);

        dl->PushClipRect(imageMin, imageMax, true);

        ImGuizmo::BeginFrame();

        if (GUIState::s_useTransformGizmo)
            DrawTransformGizmo(imageMin, imageSize);

        if (GUIState::s_useLightGizmos)
            DrawLightGizmos(imageMin, imageSize);

        DrawCameraGizmos(imageMin, imageSize);

        dl->PopClipRect();
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

        // Disable identification button interaction if Right Mouse Button is held down
        ImGuiButtonFlags flags = ImGuiButtonFlags_None;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
            flags |= ImGuiButtonFlags_PressedOnClickRelease; // Or something that prevents click?
                                                             // Actually, better to just check it here.

        ImGui::SetCursorScreenPos(iconPos);
        bool pressed = ImGui::InvisibleButton(id, textSize);
        return pressed && !ImGui::IsMouseDown(ImGuiMouseButton_Right);
    }

    void SceneView::DrawLightGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize)
    {
        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        Scene &scene = renderer->GetScene();
        Camera *camera = scene.GetActiveCamera();

        if (!camera)
            return;

        mat4 view = camera->GetView();
        mat4 proj = camera->GetProjectionNoJitter();
        mat4 viewProj = proj * view;

        // --- Helper for updating gizmo icon and selection ---
        auto checkGizmoIcon = [&](const vec3 &pos, const char *icon, LightType type, int index)
        {
            NodeId *lightNode = nullptr;
            if (type == LightType::Point && index < (int)scene.GetPointLights().size())
                lightNode = scene.GetPointLights()[index].nodeId;
            else if (type == LightType::Spot && index < (int)scene.GetSpotLights().size())
                lightNode = scene.GetSpotLights()[index].nodeId;
            else if (type == LightType::Directional && index < (int)scene.GetDirectionalLights().size())
                lightNode = scene.GetDirectionalLights()[index].nodeId;
            else if (type == LightType::Area && index < (int)scene.GetAreaLights().size())
                lightNode = scene.GetAreaLights()[index].nodeId;

            std::string id = "##LightIcon" + std::to_string((int)type) + "_" + std::to_string(index);
            auto &sel = SelectionManager::Instance();
            bool isSelected = lightNode && sel.GetSelectionType() == SelectionType::Node &&
                              sel.GetSelectedNode() == lightNode;

            if (DrawGizmoIcon(pos, icon, viewProj, imageMin, imageSize, isSelected, id.c_str()))
            {
                if (lightNode)
                    sel.Select(lightNode, SelectionType::Node);
            }
            return isSelected;
        };

        // --- Helper for drawing light visuals (Ring, Cone, etc.) ---
        auto drawLightVisuals = [&](const vec3 &pos, LightType type, int index)
        {
            ImU32 gizmoColor = IM_COL32(255, 255, 0, 255);

            if (type == LightType::Point)
            {
                float radius = scene.GetPointLights()[index].position.w;
                if (radius > 0.0f)
                {
                    GizmoUtils::DrawRing(pos, radius, vec3(1, 0, 0), viewProj, imageMin, imageSize, gizmoColor);
                    GizmoUtils::DrawRing(pos, radius, vec3(0, 1, 0), viewProj, imageMin, imageSize, gizmoColor);
                    GizmoUtils::DrawRing(pos, radius, vec3(0, 0, 1), viewProj, imageMin, imageSize, gizmoColor);
                }
            }
            else if (type == LightType::Spot)
            {
                float range = scene.GetSpotLights()[index].position.w;
                float innerAngle = scene.GetSpotLights()[index].params.x;
                float falloff = scene.GetSpotLights()[index].params.y;
                float outerAngle = innerAngle + falloff;

                vec3 dir = GetSpotDirection(scene.GetSpotLights()[index].rotation);
                vec3 baseCenter = pos + dir * range;

                float innerRadius = range * tan(glm::radians(innerAngle));
                GizmoUtils::DrawRing(baseCenter, innerRadius, dir, viewProj, imageMin, imageSize, gizmoColor);

                float outerRadius = range * tan(glm::radians(outerAngle));
                GizmoUtils::DrawRing(baseCenter, outerRadius, dir, viewProj, imageMin, imageSize, gizmoColor);

                // Draw lines from tip to base (Inner)
                ImDrawList *drawList = ImGui::GetWindowDrawList();
                vec3 tangent = glm::normalize(glm::cross(dir, vec3(0, 1, 0)));
                if (glm::length(tangent) < 0.001f)
                    tangent = glm::normalize(glm::cross(dir, vec3(1, 0, 0)));
                vec3 bitangent = glm::cross(dir, tangent);

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
                float width = scene.GetAreaLights()[index].size.x;
                float height = scene.GetAreaLights()[index].size.y;
                vec3 dir = GetDirectionFromRotation(scene.GetAreaLights()[index].rotation);

                vec3 right = glm::normalize(glm::cross(dir, vec3(0, 1, 0)));
                if (glm::length(right) < 0.001f)
                    right = glm::normalize(glm::cross(dir, vec3(1, 0, 0)));
                vec3 up = glm::normalize(glm::cross(right, dir));

                vec3 center = pos;
                vec3 pts[4] = {
                    center + (right * width * 0.5f) + (up * height * 0.5f),
                    center - (right * width * 0.5f) + (up * height * 0.5f),
                    center - (right * width * 0.5f) - (up * height * 0.5f),
                    center + (right * width * 0.5f) - (up * height * 0.5f)};

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
                float range = scene.GetAreaLights()[index].position.w;
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
                const auto &dirLight = scene.GetDirectionalLights()[index];
                glm::quat rot = glm::quat(dirLight.rotation.w, dirLight.rotation.x, dirLight.rotation.y, dirLight.rotation.z);
                vec3 dir = glm::normalize(rot * vec3(0, 0, -1));
                vec3 center = pos;
                vec3 end = center + dir * 2.0f;

                ImDrawList *drawList = ImGui::GetWindowDrawList();

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
                }

                GizmoUtils::DrawRing(center, 0.5f, dir, viewProj, imageMin, imageSize, gizmoColor);
            }
        };

        // Point Lights
        for (int i = 0; i < (int)scene.GetPointLights().size(); i++)
        {
            if (checkGizmoIcon(vec3(scene.GetPointLights()[i].position), ICON_FA_LIGHTBULB, LightType::Point, i))
                drawLightVisuals(vec3(scene.GetPointLights()[i].position), LightType::Point, i);
        }

        // Spot Lights
        for (int i = 0; i < (int)scene.GetSpotLights().size(); i++)
        {
            if (checkGizmoIcon(vec3(scene.GetSpotLights()[i].position), ICON_FA_LIGHTBULB, LightType::Spot, i))
                drawLightVisuals(vec3(scene.GetSpotLights()[i].position), LightType::Spot, i);
        }

        // Directional Lights
        for (int i = 0; i < (int)scene.GetDirectionalLights().size(); i++)
        {
            if (checkGizmoIcon(vec3(scene.GetDirectionalLights()[i].position), ICON_FA_SUN, LightType::Directional, i))
                drawLightVisuals(vec3(scene.GetDirectionalLights()[i].position), LightType::Directional, i);
        }

        // Area Lights
        for (int i = 0; i < (int)scene.GetAreaLights().size(); i++)
        {
            if (checkGizmoIcon(vec3(scene.GetAreaLights()[i].position), ICON_FA_LIGHTBULB, LightType::Area, i))
                drawLightVisuals(vec3(scene.GetAreaLights()[i].position), LightType::Area, i);
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
            NodeId *camNode = camera->GetNodeId();

            vec3 pos = camera->GetPosition();
            bool isSelected = camNode && selection.GetSelectionType() == SelectionType::Node &&
                              selection.GetSelectedNode() == camNode;

            std::string iconId = "##CameraIcon" + std::to_string(i);
            if (DrawGizmoIcon(pos, ICON_FA_VIDEO, viewProj, imageMin, imageSize, isSelected, iconId.c_str()))
            {
                if (camNode)
                    selection.Select(camNode, SelectionType::Node);
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
