#include "SceneView.h"
#include "API/Image.h"
#include "Camera/Camera.h"
#include "GUI/GUI.h"
#include "GUI/GUIState.h"
#include "GUI/Helpers.h"
#include "GUI/IconsFontAwesome.h"
#include "GUI/RuntimeUiAuthoring.h"
#include "GUI/SkinnedStrip2DEditor.h"
#include "GUI/UndoRedo.h"
#include "Particles/ParticleManager.h"
#include "Scene/ModelAsset.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneHost.h"
#include "Scene/SceneNode.h"
#include "Scene/SelectionManager.h"
#include "Script/Bindings/Input/InputState.h"
#include "Systems/AnimationSystem.h"
#include "Systems/RendererSystem.h"
#include "UI/RuntimeUi.h"
#include "imgui/ImGuizmo.h"
#include "imgui/imgui_internal.h"

namespace pe
{
    namespace
    {
        bool s_orientationGizmoHovered = false;
        bool s_orientationGizmoActive = false;
        bool s_stripIkGizmoHovered = false;
        bool s_stripIkGizmoActive = false;
    } // namespace

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

    static bool IsRuntimeUiNode(Scene &scene, NodeId *node)
    {
        return node &&
               scene.IsNodeAlive(node) &&
               scene.IsNodeHierarchyEnabled(node) &&
               (scene.GetComponentFlags(node) & Component_RuntimeUi) != 0;
    }

    static bool GetRuntimeUiSurfaceSize(float &surfaceWidth, float &surfaceHeight)
    {
        RuntimeUiSystem *runtimeUi = GetActiveRuntimeUi();
        if (!runtimeUi)
            return false;

        uint32_t width = 0;
        uint32_t height = 0;
        runtimeUi->GetFrameSurfaceSize(width, height);
        if (width == 0 || height == 0)
            return false;

        surfaceWidth = static_cast<float>(width);
        surfaceHeight = static_cast<float>(height);
        return true;
    }

    static mat4 ComputeNodeWorldMatrix(Scene &scene, NodeId *node)
    {
        if (!node || !scene.IsNodeAlive(node))
            return mat4(1.0f);

        NodeId *parent = scene.GetParent(node);
        if (!parent)
            return scene.GetLocalMatrix(node);

        return ComputeNodeWorldMatrix(scene, parent) * scene.GetLocalMatrix(node);
    }

    static bool ProjectWorldToViewport(const vec3 &world,
                                       const mat4 &viewProj,
                                       const ImVec2 &imageMin,
                                       const ImVec2 &imageSize,
                                       ImVec2 &screen)
    {
        const vec4 clipPos = viewProj * vec4(world, 1.0f);
        if (clipPos.w <= 0.0f)
            return false;

        const vec2 ndcPos = vec2(clipPos) / clipPos.w;
        screen.x = (ndcPos.x * 0.5f + 0.5f) * imageSize.x + imageMin.x;
        screen.y = (ndcPos.y * 0.5f + 0.5f) * imageSize.y + imageMin.y;
        return std::isfinite(screen.x) && std::isfinite(screen.y);
    }

    static bool BuildViewportRay(Camera *camera, float normalizedX, float normalizedY, vec3 &rayOrigin, vec3 &rayDir)
    {
        if (!camera)
            return false;

        const float ndcX = normalizedX * 2.0f - 1.0f;
        const float ndcY = normalizedY * 2.0f - 1.0f;

        const mat4 invViewProj = camera->GetInvViewProjection();
        // Reverse-Z projection: near = 1, far = 0.
        vec4 nearPoint = invViewProj * vec4(ndcX, ndcY, 1.0f, 1.0f);
        if (std::abs(nearPoint.w) < 1e-6f)
            return false;
        nearPoint /= nearPoint.w;

        vec4 farPoint = invViewProj * vec4(ndcX, ndcY, 0.0f, 1.0f);
        rayOrigin = vec3(nearPoint);
        if (std::abs(farPoint.w) < 1e-6f)
            rayDir = normalize(vec3(farPoint));
        else
        {
            farPoint /= farPoint.w;
            rayDir = normalize(vec3(farPoint) - rayOrigin);
        }

        return std::isfinite(rayOrigin.x) && std::isfinite(rayOrigin.y) && std::isfinite(rayOrigin.z) &&
               std::isfinite(rayDir.x) && std::isfinite(rayDir.y) && std::isfinite(rayDir.z);
    }

    static bool BuildViewportRay(Camera *camera,
                                 const ImVec2 &mouse,
                                 const ImVec2 &imageMin,
                                 const ImVec2 &imageSize,
                                 vec3 &rayOrigin,
                                 vec3 &rayDir)
    {
        if (imageSize.x <= 0.0f || imageSize.y <= 0.0f)
            return false;

        const float normalizedX = (mouse.x - imageMin.x) / imageSize.x;
        const float normalizedY = (mouse.y - imageMin.y) / imageSize.y;
        return BuildViewportRay(camera, normalizedX, normalizedY, rayOrigin, rayDir);
    }

    static bool ScreenPointToStripLocalXY(Scene &scene,
                                          NodeId *node,
                                          Camera *camera,
                                          const ImVec2 &mouse,
                                          const ImVec2 &imageMin,
                                          const ImVec2 &imageSize,
                                          vec2 &localXY)
    {
        vec3 rayOrigin(0.0f);
        vec3 rayDir(0.0f);
        if (!BuildViewportRay(camera, mouse, imageMin, imageSize, rayOrigin, rayDir))
            return false;

        const mat4 nodeWorld = ComputeNodeWorldMatrix(scene, node);
        const float det = glm::determinant(nodeWorld);
        if (std::abs(det) < 1e-6f)
            return false;

        const vec3 planePoint = vec3(nodeWorld[3]);
        const vec3 planeNormal = normalize(mat3(nodeWorld) * vec3(0.0f, 0.0f, 1.0f));
        const float denom = dot(rayDir, planeNormal);
        if (std::abs(denom) < 1e-6f)
            return false;

        const float t = dot(planePoint - rayOrigin, planeNormal) / denom;
        if (t < 0.0f)
            return false;

        const vec3 worldPoint = rayOrigin + rayDir * t;
        const vec4 localPoint = inverse(nodeWorld) * vec4(worldPoint, 1.0f);
        if (std::abs(localPoint.w) < 1e-6f)
            return false;

        localXY = vec2(localPoint) / localPoint.w;
        return std::isfinite(localXY.x) && std::isfinite(localXY.y);
    }

    static bool DecomposeRuntimeUiRect(const mat4 &matrix, float &x, float &y, float &w, float &h)
    {
        float t[3] = {};
        float r[3] = {};
        float s[3] = {};
        ImGuizmo::DecomposeMatrixToComponents(value_ptr(matrix), t, r, s);
        x = t[0];
        y = t[1];
        w = s[0];
        h = s[1];
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(w) && std::isfinite(h) &&
               w > 0.0f && h > 0.0f;
    }

    static bool MergeRuntimeUiRect(float srcX, float srcY, float srcW, float srcH,
                                   bool &found, float &x, float &y, float &w, float &h)
    {
        if (srcW <= 0.0f || srcH <= 0.0f)
            return false;

        if (!found)
        {
            x = srcX;
            y = srcY;
            w = srcW;
            h = srcH;
            found = true;
            return true;
        }

        const float minX = std::min(x, srcX);
        const float minY = std::min(y, srcY);
        const float maxX = std::max(x + w, srcX + srcW);
        const float maxY = std::max(y + h, srcY + srcH);
        x = minX;
        y = minY;
        w = maxX - minX;
        h = maxY - minY;
        return true;
    }

    static bool GetRuntimeUiDescendantRect(Scene &scene, NodeId *node, float &x, float &y, float &w, float &h)
    {
        bool found = false;
        RuntimeUiSystem *runtimeUi = GetActiveRuntimeUi();
        if (runtimeUi)
        {
            float directX = 0.0f;
            float directY = 0.0f;
            float directW = 0.0f;
            float directH = 0.0f;
            if (runtimeUi->GetNodeRect(node, directX, directY, directW, directH))
                MergeRuntimeUiRect(directX, directY, directW, directH, found, x, y, w, h);
        }

        for (NodeId *child : scene.GetChildren(node))
        {
            if (!IsRuntimeUiNode(scene, child))
                continue;

            float childX = 0.0f;
            float childY = 0.0f;
            float childW = 0.0f;
            float childH = 0.0f;
            if (GetRuntimeUiDescendantRect(scene, child, childX, childY, childW, childH))
                MergeRuntimeUiRect(childX, childY, childW, childH, found, x, y, w, h);
        }

        return found;
    }

    static bool GetRuntimeUiNodeRect(Scene &scene, NodeId *node, float &x, float &y, float &w, float &h)
    {
        if (!IsRuntimeUiNode(scene, node))
            return false;

        if (GetRuntimeUiDescendantRect(scene, node, x, y, w, h))
            return true;

        return DecomposeRuntimeUiRect(ComputeNodeWorldMatrix(scene, node), x, y, w, h);
    }

    static bool HasDirectRuntimeUiNodeRect(NodeId *node)
    {
        RuntimeUiSystem *runtimeUi = GetActiveRuntimeUi();
        if (!runtimeUi)
            return false;

        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        return runtimeUi->GetNodeRect(node, x, y, w, h);
    }

    static void SetRuntimeUiNodeWorldMatrix(Scene &scene, NodeId *node, const mat4 &world)
    {
        NodeId *parent = scene.GetParent(node);
        const mat4 parentWorld = parent ? ComputeNodeWorldMatrix(scene, parent) : mat4(1.0f);
        const float det = glm::determinant(parentWorld);
        if (std::abs(det) < 1e-6f)
            return;

        scene.SetLocalMatrix(node, glm::inverse(parentWorld) * world);
    }

    static void SetRuntimeUiNodeRect(Scene &scene, NodeId *node,
                                     float previousX, float previousY, float previousW, float previousH,
                                     float x, float y, float w, float h)
    {
        if (!IsRuntimeUiNode(scene, node))
            return;

        w = std::max(w, 1.0f);
        h = std::max(h, 1.0f);

        if (HasDirectRuntimeUiNodeRect(node))
        {
            // (x,y) is the desired rendered TOP-LEFT. The node translation stores the
            // offset from the screen anchor to the element pivot, so invert:
            //   translation = topLeft - anchor*surface + pivot*size
            float surfaceWidth = 0.0f;
            float surfaceHeight = 0.0f;
            GetRuntimeUiSurfaceSize(surfaceWidth, surfaceHeight);
            vec2 anchor(0.0f, 0.0f);
            vec2 pivot(0.5f, 0.5f);
            if (const NodeRuntimeUiTag *ui = scene.GetRuntimeUiComponent(node))
            {
                anchor = ui->anchor;
                pivot = ui->pivot;
            }
            const float tx = x - anchor.x * surfaceWidth + pivot.x * w;
            const float ty = y - anchor.y * surfaceHeight + pivot.y * h;
            const mat4 world = glm::translate(mat4(1.0f), vec3(tx, ty, 0.0f)) *
                               glm::scale(mat4(1.0f), vec3(w, h, 1.0f));
            SetRuntimeUiNodeWorldMatrix(scene, node, world);
            return;
        }

        mat4 world = ComputeNodeWorldMatrix(scene, node);
        float t[3] = {};
        float r[3] = {};
        float s[3] = {};
        ImGuizmo::DecomposeMatrixToComponents(value_ptr(world), t, r, s);
        if (!std::isfinite(t[0]) || !std::isfinite(t[1]))
            return;

        const float dx = x - previousX;
        const float dy = y - previousY;
        float sx = s[0];
        float sy = s[1];
        if (previousW > 0.0f && previousH > 0.0f)
        {
            sx *= w / previousW;
            sy *= h / previousH;
        }

        world = glm::translate(mat4(1.0f), vec3(t[0] + dx, t[1] + dy, t[2])) *
                glm::rotate(mat4(1.0f), glm::radians(r[2]), vec3(0.0f, 0.0f, 1.0f)) *
                glm::scale(mat4(1.0f), vec3(sx, sy, s[2]));
        SetRuntimeUiNodeWorldMatrix(scene, node, world);
    }

    static NodeId *PickRuntimeUiNodeAt(const ImVec2 &mouse, const ImVec2 &imageMin, const ImVec2 &imageSize)
    {
        RuntimeUiSystem *runtimeUi = GetActiveRuntimeUi();
        if (!runtimeUi || imageSize.x <= 0.0f || imageSize.y <= 0.0f)
            return nullptr;

        float surfaceWidth = 0.0f;
        float surfaceHeight = 0.0f;
        if (!GetRuntimeUiSurfaceSize(surfaceWidth, surfaceHeight))
            return nullptr;

        const float surfaceX = (mouse.x - imageMin.x) * surfaceWidth / imageSize.x;
        const float surfaceY = (mouse.y - imageMin.y) * surfaceHeight / imageSize.y;
        NodeId *node = runtimeUi->PickNode(surfaceX, surfaceY);
        if (!node)
            return nullptr;

        Scene *scene = GetActiveScene();
        return scene && IsRuntimeUiNode(*scene, node) ? node : nullptr;
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

        static bool IsFinite(const glm::vec3 &v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        static void ApplyCameraOrientationFromDirection(Camera *camera, const glm::vec3 &direction)
        {
            if (!camera)
                return;

            const float len = glm::length(direction);
            if (!std::isfinite(len) || len < 1e-6f)
                return;

            glm::vec3 front = direction / len;
            if (!IsFinite(front))
                return;

            const float pitch = -std::asin(glm::clamp(front.y, -1.0f, 1.0f));
            float yaw = camera->GetEuler().y;
            if (std::abs(front.x) > 1e-5f || std::abs(front.z) > 1e-5f)
                yaw = std::atan2(front.x, front.z);

            camera->SetEuler(glm::vec3(pitch, yaw, 0.0f));
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
                    Scene &scene = *GetActiveScene();
                    if (!scene.IsNodeAlive(ctx.node))
                    {
                        selection.ClearSelection();
                        ctx.node = nullptr;
                        return ctx;
                    }
                    if (!scene.IsNodeHierarchyEnabled(ctx.node))
                        return ctx;
                    ctx.matrix = scene.GetWorldMatrix(ctx.node);
                    ctx.valid = true;
                }
            }
            else if (selection.GetSelectionType() == SelectionType::Light)
            {
                ctx.lightType = selection.GetSelectedLightType();
                ctx.lightIndex = selection.GetSelectedLightIndex();
                ctx.useLight = true;

                Scene &scene = *GetActiveScene();
                if (ctx.lightType == LightType::Point && ctx.lightIndex >= 0 && ctx.lightIndex < (int)scene.GetPointLights().size())
                {
                    const auto &pointLight = scene.GetPointLights()[ctx.lightIndex];
                    if (!scene.IsNodeHierarchyEnabled(pointLight.nodeId))
                        return ctx;
                    ctx.matrix = glm::translate(glm::mat4(1.0f), glm::vec3(pointLight.position));
                    ctx.valid = true;
                }
                else if (ctx.lightType == LightType::Spot && ctx.lightIndex >= 0 && ctx.lightIndex < (int)scene.GetSpotLights().size())
                {
                    const auto &spot = scene.GetSpotLights()[ctx.lightIndex];
                    if (!scene.IsNodeHierarchyEnabled(spot.nodeId))
                        return ctx;
                    glm::vec3 start = glm::vec3(spot.position);
                    glm::quat rot = glm::quat(spot.rotation.w, spot.rotation.x, spot.rotation.y, spot.rotation.z);
                    ctx.matrix = glm::translate(glm::mat4(1.0f), start) * glm::mat4_cast(rot);
                    ctx.valid = true;
                }
                else if (ctx.lightType == LightType::Directional && ctx.lightIndex >= 0 && ctx.lightIndex < (int)scene.GetDirectionalLights().size())
                {
                    const auto &dirLight = scene.GetDirectionalLights()[ctx.lightIndex];
                    if (!scene.IsNodeHierarchyEnabled(dirLight.nodeId))
                        return ctx;
                    glm::vec3 pos = glm::vec3(dirLight.position);
                    glm::quat rot = glm::quat(dirLight.rotation.w, dirLight.rotation.x, dirLight.rotation.y, dirLight.rotation.z);
                    ctx.matrix = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot);
                    ctx.valid = true;
                }
                else if (ctx.lightType == LightType::Area && ctx.lightIndex >= 0 && ctx.lightIndex < (int)scene.GetAreaLights().size())
                {
                    const auto &area = scene.GetAreaLights()[ctx.lightIndex];
                    if (!scene.IsNodeHierarchyEnabled(area.nodeId))
                        return ctx;
                    glm::vec3 start = glm::vec3(area.position);
                    glm::quat rot = glm::quat(area.rotation.w, area.rotation.x, area.rotation.y, area.rotation.z);
                    ctx.matrix = glm::translate(glm::mat4(1.0f), start) * glm::mat4_cast(rot);
                    ctx.valid = true;
                }
            }
            else if (selection.GetSelectionType() == SelectionType::Camera)
            {
                ctx.cameraIndex = selection.GetSelectedCameraIndex();
                auto cameras = GetActiveScene()->GetCameras();
                if (ctx.cameraIndex >= 0 && ctx.cameraIndex < (int)cameras.size())
                {
                    Camera *c = cameras[ctx.cameraIndex];
                    Scene &scene = *GetActiveScene();
                    NodeId *camNode = c->GetNodeId();
                    if (camNode && !scene.IsNodeHierarchyEnabled(camNode))
                        return ctx;
                    glm::vec3 pos = c->GetPosition();
                    glm::quat rot = glm::quat(c->GetEuler());
                    ctx.matrix = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot);
                    ctx.valid = true;
                }
            }
            else if (selection.GetSelectionType() == SelectionType::Emitter)
            {
                ctx.emitterIndex = selection.GetSelectedEmitterIndex();
                ParticleManager *pm = GetActiveScene()->GetParticleManager();
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
                Scene &scene = *GetActiveScene();

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
                auto cameras = GetActiveScene()->GetCameras();
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
                ParticleManager *pm = GetActiveScene()->GetParticleManager();
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

                Scene &scene = *GetActiveScene();
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

        const ImVec2 panelMin = ImGui::GetCursorScreenPos();
        const ImVec2 panelMax(panelMin.x + panel.x, panelMin.y + panel.y);
        ImGui::GetWindowDrawList()->AddRectFilled(panelMin, panelMax, IM_COL32(0, 0, 0, 255));

        const float sourceAspect = (float)sceneTexture->GetWidth() / (float)sceneTexture->GetHeight();
        const float targetAspect = GUIState::GetSceneViewAspectRatio(sourceAspect);
        const ImVec2 imageSize = ComputeAspectFitSize(panel, targetAspect);

        CenterCursorForImage(panel, imageSize);
        const ImVec2 uv0(0.0f, 0.0f);
        const ImVec2 uv1(1.0f, 1.0f);
        ImGui::Image((ImTextureID)(intptr_t)GUIState::s_viewportTextureId, imageSize, uv0, uv1);

        const ImVec2 imageMin = ImGui::GetItemRectMin();
        const ImVec2 imageMax = ImGui::GetItemRectMax();
        const bool imageHovered = ImGui::IsItemHovered();
        const ImGuiViewport *imageViewport = ImGui::GetWindowViewport();
        const ImVec2 viewportOrigin = imageViewport ? imageViewport->Pos : ImVec2(0.0f, 0.0f);
        GUIState::s_sceneViewImageRectValid = true;
        GUIState::s_sceneViewImageMinX = imageMin.x - viewportOrigin.x;
        GUIState::s_sceneViewImageMinY = imageMin.y - viewportOrigin.y;
        GUIState::s_sceneViewImageAbsMinX = imageMin.x;
        GUIState::s_sceneViewImageAbsMinY = imageMin.y;
        GUIState::s_sceneViewImageWidth = imageMax.x - imageMin.x;
        GUIState::s_sceneViewImageHeight = imageMax.y - imageMin.y;

        const bool playMode = GUIState::s_playMode;
        if (!playMode)
            DrawGizmos(imageMin, imageSize);

        const bool overGizmo = !playMode && (ImGuizmo::IsOver() || s_orientationGizmoHovered || s_stripIkGizmoHovered);
        const bool usingGizmo = !playMode && (ImGuizmo::IsUsing() || s_orientationGizmoActive || s_stripIkGizmoActive);
        const bool runtimeUiMouseCaptured = InputState::IsMouseCapturedByUi();

        // Input (picking / focus) only when not interacting with gizmos
        if (!playMode && !overGizmo && !usingGizmo)
        {
            if (imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                const ImVec2 mouse = ImGui::GetMousePos();
                if (NodeId *uiNode = PickRuntimeUiNodeAt(mouse, imageMin, imageSize))
                {
                    SelectionManager::Instance().Select(uiNode, SelectionType::Node);
                }
                else if (!runtimeUiMouseCaptured)
                {
                    const float nx = (mouse.x - imageMin.x) / (imageMax.x - imageMin.x);
                    const float ny = (mouse.y - imageMin.y) / (imageMax.y - imageMin.y);
                    PerformObjectPicking(nx, ny);
                }
            }

            if (imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                ImGui::SetWindowFocus();
            }
        }

        // Drag&drop
        const ImGuiID viewportDropId = ImGui::GetID("ViewportRuntimeUiDropTarget");
        if (!playMode && ImGui::BeginDragDropTargetCustom(ImRect(imageMin, imageMax), viewportDropId))
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                const char *pathStr = (const char *)payload->Data;
                std::filesystem::path path(pathStr);

                std::string ext = path.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                // Only cooked .pemesh loads into the scene; source models are import-only (File > Import).
                const bool isModel = (ext == ".pemesh");

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

            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(RuntimeUiAuthoring::kDragPayloadType))
            {
                if (payload->DataSize == sizeof(RuntimeUiAuthoring::DragPayload))
                {
                    const auto data = *static_cast<const RuntimeUiAuthoring::DragPayload *>(payload->Data);
                    RendererSystem *dropRenderer = GetGlobalSystem<RendererSystem>();
                    if (dropRenderer)
                    {
                        float surfaceWidth = 0.0f;
                        float surfaceHeight = 0.0f;
                        if (!GetRuntimeUiSurfaceSize(surfaceWidth, surfaceHeight))
                        {
                            surfaceWidth = imageSize.x;
                            surfaceHeight = imageSize.y;
                        }

                        const RuntimeUiAuthoring::ElementTemplate *element = RuntimeUiAuthoring::FindTemplate(data.type);
                        const vec2 elementSize = element ? element->size : vec2(240.0f, 120.0f);
                        const ImVec2 mouse = ImGui::GetMousePos();
                        const float surfaceX = (mouse.x - imageMin.x) * surfaceWidth / imageSize.x;
                        const float surfaceY = (mouse.y - imageMin.y) * surfaceHeight / imageSize.y;
                        const float x = glm::clamp(surfaceX - elementSize.x * 0.5f, 0.0f, std::max(0.0f, surfaceWidth - elementSize.x));
                        const float y = glm::clamp(surfaceY - elementSize.y * 0.5f, 0.0f, std::max(0.0f, surfaceHeight - elementSize.y));

                        Scene &scene = dropRenderer->GetScene();
                        UndoRedo::Instance().RecordSnapshot(scene, "Added UI Element");
                        SyncSceneBeforeMutation();
                        NodeId *node = RuntimeUiAuthoring::CreateNode(scene, data.type, vec2(x, y));
                        SelectionManager::Instance().Select(node, SelectionType::Node);
                        if (m_gui)
                            m_gui->NotifyChange();
                    }
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

        vec3 rayOrigin(0.0f);
        vec3 rayDir(0.0f);
        if (!BuildViewportRay(camera, normalizedX, normalizedY, rayOrigin, rayDir))
            return;
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
            if (!scene.IsNodeHierarchyEnabled(node))
                continue;
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

    bool SceneView::DrawRuntimeUiTransformGizmo(const ImVec2 &imageMin, const ImVec2 &imageSize)
    {
        auto &selection = SelectionManager::Instance();
        if (!selection.HasSelection() || selection.GetSelectionType() != SelectionType::Node)
            return false;

        Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();
        NodeId *node = selection.GetSelectedNode();
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        if (!GetRuntimeUiNodeRect(scene, node, x, y, w, h))
            return false;

        float surfaceWidth = 0.0f;
        float surfaceHeight = 0.0f;
        if (!GetRuntimeUiSurfaceSize(surfaceWidth, surfaceHeight))
            return false;

        const float sx = imageSize.x / surfaceWidth;
        const float sy = imageSize.y / surfaceHeight;
        ImRect rect(ImVec2(imageMin.x + x * sx, imageMin.y + y * sy),
                    ImVec2(imageMin.x + (x + w) * sx, imageMin.y + (y + h) * sy));

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const ImU32 border = IM_COL32(255, 214, 74, 255);
        const ImU32 fill = IM_COL32(255, 214, 74, 28);
        drawList->AddRectFilled(rect.Min, rect.Max, fill, 4.0f);
        drawList->AddRect(rect.Min, rect.Max, border, 4.0f, 0, 2.0f);

        ImGuizmo::SetOrthographic(true);
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(imageMin.x, imageMin.y, imageSize.x, imageSize.y);

        const bool isRightClick = ImGui::IsMouseDown(ImGuiMouseButton_Right) && ImGui::IsWindowFocused();
        ImGuizmo::Enable(!isRightClick);

        ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
        if (selection.GetGizmoOperation() == GizmoOperation::Scale)
            op = ImGuizmo::SCALE;

        mat4 view(1.0f);
        mat4 projection = glm::orthoRH_NO(0.0f, surfaceWidth, surfaceHeight, 0.0f, -1.0f, 1.0f);
        mat4 matrix = glm::translate(mat4(1.0f), vec3(x + w * 0.5f, y + h * 0.5f, 0.0f)) *
                      glm::scale(mat4(1.0f), vec3(w, h, 1.0f));

        if (ImGuizmo::Manipulate(value_ptr(view), value_ptr(projection), op, ImGuizmo::LOCAL, value_ptr(matrix)))
        {
            float t[3] = {};
            float r[3] = {};
            float s[3] = {};
            ImGuizmo::DecomposeMatrixToComponents(value_ptr(matrix), t, r, s);

            const float newW = std::max(std::fabs(s[0]), 1.0f);
            const float newH = std::max(std::fabs(s[1]), 1.0f);
            if (std::isfinite(t[0]) && std::isfinite(t[1]) &&
                std::isfinite(newW) && std::isfinite(newH))
            {
                SetRuntimeUiNodeRect(scene,
                                     node,
                                     x,
                                     y,
                                     w,
                                     h,
                                     t[0] - newW * 0.5f,
                                     t[1] - newH * 0.5f,
                                     newW,
                                     newH);
            }
        }

        return true;
    }

    void SceneView::DrawSkinnedStrip2DIkGizmo(const ImVec2 &imageMin, const ImVec2 &imageSize)
    {
        auto &selection = SelectionManager::Instance();
        if (!selection.HasSelection() || selection.GetSelectionType() != SelectionType::Node)
            return;

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        if (!renderer)
            return;

        Scene &scene = renderer->GetScene();
        NodeId *node = selection.GetSelectedNode();
        if (!node || !scene.IsNodeAlive(node) || !scene.NodeUsesSkinnedStrip2D(node))
            return;

        AnimationSystem *anim = GetGlobalSystem<AnimationSystem>();
        Camera *camera = scene.GetActiveCamera();
        if (!anim || !camera)
            return;

        auto &state = skinned_strip_2d_editor::GetState(scene, node);
        const mat4 nodeWorld = ComputeNodeWorldMatrix(scene, node);
        const vec3 targetWorld = vec3(nodeWorld * vec4(state.ikTargetLocal, 0.0f, 1.0f));
        const mat4 viewProj = camera->GetProjectionNoJitter() * camera->GetView();

        ImVec2 targetScreen;
        if (!ProjectWorldToViewport(targetWorld, viewProj, imageMin, imageSize, targetScreen))
            return;

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const ImU32 lineColor = IM_COL32(63, 205, 190, 180);
        const ImU32 fillColor = IM_COL32(63, 205, 190, 230);
        const ImU32 hoverColor = IM_COL32(255, 226, 102, 255);
        std::vector<vec2> jointPositions;
        if (skinned_strip_2d_editor::GetPoseJointPositionsLocal(scene, node, state, jointPositions))
        {
            ImVec2 previousScreen;
            bool hasPrevious = false;
            for (const vec2 &jointLocal : jointPositions)
            {
                const vec3 jointWorld = vec3(nodeWorld * vec4(jointLocal, 0.0f, 1.0f));
                ImVec2 jointScreen;
                if (!ProjectWorldToViewport(jointWorld, viewProj, imageMin, imageSize, jointScreen))
                {
                    hasPrevious = false;
                    continue;
                }

                drawList->AddCircleFilled(jointScreen, 3.5f, IM_COL32(63, 205, 190, 180), 12);
                if (hasPrevious)
                    drawList->AddLine(previousScreen, jointScreen, lineColor, 1.5f);
                previousScreen = jointScreen;
                hasPrevious = true;
            }

            if (!jointPositions.empty())
            {
                const vec3 rootWorld = vec3(nodeWorld * vec4(jointPositions.front(), 0.0f, 1.0f));
                ImVec2 rootScreen;
                if (ProjectWorldToViewport(rootWorld, viewProj, imageMin, imageSize, rootScreen))
                    drawList->AddLine(rootScreen, targetScreen, IM_COL32(63, 205, 190, 95), 1.0f);
            }
        }

        constexpr float kRadius = 8.0f;
        ImGui::SetCursorScreenPos(ImVec2(targetScreen.x - kRadius, targetScreen.y - kRadius));
        ImGui::InvisibleButton("##skinned_strip_2d_ik_target", ImVec2(kRadius * 2.0f, kRadius * 2.0f));

        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        s_stripIkGizmoHovered = hovered;
        s_stripIkGizmoActive = active;

        drawList->AddCircleFilled(targetScreen, kRadius, (hovered || active) ? hoverColor : fillColor, 24);
        drawList->AddCircle(targetScreen, kRadius + 2.0f, IM_COL32(24, 38, 38, 220), 24, 1.5f);
        drawList->AddCircle(targetScreen, kRadius + 4.0f, lineColor, 24, 1.0f);

        if (hovered)
            ui::TooltipText("Drag IK target");

        if (!active || ImGui::IsMouseDown(ImGuiMouseButton_Right))
            return;

        vec2 localTarget(0.0f);
        if (!ScreenPointToStripLocalXY(scene, node, camera, ImGui::GetMousePos(), imageMin, imageSize, localTarget))
            return;

        state.ikTargetLocal = localTarget;
        if (skinned_strip_2d_editor::SolveIk(anim, scene, node, state) && m_gui)
            m_gui->NotifyChange();
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

        ImGuizmo::SetOrthographic(cam->IsOrthographic());
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(imageMin.x, imageMin.y, imageSize.x, imageSize.y);

        // Disable interaction if Right Mouse Button is held down inside this window
        const bool isRightClick = ImGui::IsMouseDown(ImGuiMouseButton_Right) && ImGui::IsWindowFocused();
        ImGuizmo::Enable(!isRightClick);

        // --- Build matrices for ImGuizmo in RH space ---
        // Engine is LH, so convert view to RH.
        glm::mat4 viewLH = cam->GetView();
        glm::mat4 viewRH = GizmoUtils::FlipHandedness(viewLH);

        float aspect = cam->GetAspect();
        float nearPlane = glm::max(0.001f, cam->GetNearPlane());
        float farPlane = 1000.0f;
        glm::mat4 projRH;
        if (cam->IsOrthographic())
        {
            float halfHeight = glm::max(0.001f, cam->GetOrthographicSize()) * 0.5f;
            float halfWidth = halfHeight * aspect;
            projRH = glm::orthoRH_NO(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, farPlane);
        }
        else
        {
            projRH = glm::perspectiveRH_NO(cam->Fovy(), aspect, nearPlane, farPlane);
        }

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

    void SceneView::DrawOrientationGizmo(const ImVec2 &imageMin, const ImVec2 &imageSize)
    {
        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        if (!renderer)
            return;

        Camera *camera = renderer->GetScene().GetActiveCamera();
        if (!camera || imageSize.x < 96.0f || imageSize.y < 96.0f)
            return;

        const float edge = glm::clamp(glm::min(imageSize.x, imageSize.y) * 0.16f, 88.0f, 128.0f);
        const float pad = 12.0f;
        const ImVec2 pos(imageMin.x + imageSize.x - edge - pad, imageMin.y + pad);
        const ImVec2 size(edge, edge);
        const ImVec2 center(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
        const float radius = size.x * 0.38f;
        const float hitRadius = 16.0f;

        struct OrientationAxis
        {
            const char *name = "";
            const char *letter = "";
            glm::vec3 direction = glm::vec3(0.0f);
            ImU32 color = 0;
            ImVec2 end = ImVec2(0.0f, 0.0f);
            ImVec2 dir2 = ImVec2(0.0f, 0.0f);
            float projectedLength = 0.0f;
            float depth = 0.0f;
            bool hovered = false;
        };

        OrientationAxis axes[3] = {
            {"Right", "X", glm::vec3(1.0f, 0.0f, 0.0f), IM_COL32(235, 74, 74, 255)},
            {"Front", "Z", glm::vec3(0.0f, 0.0f, 1.0f), IM_COL32(74, 137, 255, 255)},
            {"Up", "Y", glm::vec3(0.0f, 1.0f, 0.0f), IM_COL32(74, 210, 116, 255)},
        };

        const glm::vec3 cameraRight = glm::normalize(camera->GetRight());
        const glm::vec3 cameraUp = glm::normalize(camera->GetUp());
        const glm::vec3 cameraFront = glm::normalize(camera->GetFront());

        for (OrientationAxis &axis : axes)
        {
            const glm::vec3 worldDir = glm::normalize(axis.direction);
            const glm::vec2 projected(glm::dot(worldDir, cameraRight), glm::dot(worldDir, cameraUp));
            axis.depth = glm::dot(worldDir, cameraFront);
            axis.projectedLength = glm::length(projected);

            if (axis.projectedLength > 1e-4f)
            {
                const glm::vec2 dir = projected / axis.projectedLength;
                const float drawLength = radius * glm::clamp(axis.projectedLength, 0.25f, 1.0f);
                axis.dir2 = ImVec2(dir.x, dir.y);
                axis.end = ImVec2(center.x + dir.x * drawLength, center.y + dir.y * drawLength);
            }
            else
            {
                axis.end = center;
            }
        }

        const ImVec2 mouse = ImGui::GetMousePos();
        const ImRect gizmoRect(pos, ImVec2(pos.x + size.x, pos.y + size.y));
        const bool canInteract = ImGui::IsWindowHovered() && gizmoRect.Contains(mouse) && !ImGui::IsMouseDown(ImGuiMouseButton_Right);

        auto distanceToSegment = [](const ImVec2 &p, const ImVec2 &a, const ImVec2 &b)
        {
            const ImVec2 ab(b.x - a.x, b.y - a.y);
            const ImVec2 ap(p.x - a.x, p.y - a.y);
            const float abLenSq = ab.x * ab.x + ab.y * ab.y;
            float t = 0.0f;
            if (abLenSq > 1e-6f)
                t = glm::clamp((ap.x * ab.x + ap.y * ab.y) / abLenSq, 0.0f, 1.0f);

            const ImVec2 closest(a.x + ab.x * t, a.y + ab.y * t);
            const float dx = p.x - closest.x;
            const float dy = p.y - closest.y;
            return std::sqrt(dx * dx + dy * dy);
        };

        int hoveredIndex = -1;
        float bestDistance = hitRadius;
        if (canInteract)
        {
            for (int i = 0; i < 3; ++i)
            {
                const float endpointDx = mouse.x - axes[i].end.x;
                const float endpointDy = mouse.y - axes[i].end.y;
                float dist = std::sqrt(endpointDx * endpointDx + endpointDy * endpointDy);
                if (axes[i].projectedLength > 0.12f)
                    dist = std::min(dist, distanceToSegment(mouse, center, axes[i].end));

                if (dist < bestDistance)
                {
                    bestDistance = dist;
                    hoveredIndex = i;
                }
            }
        }

        if (hoveredIndex >= 0)
        {
            axes[hoveredIndex].hovered = true;
            s_orientationGizmoHovered = true;
            s_orientationGizmoActive = ImGui::IsMouseDown(ImGuiMouseButton_Left);

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                GizmoUtils::ApplyCameraOrientationFromDirection(camera, axes[hoveredIndex].direction);

            ImGui::SetTooltip("Snap the camera to the %s view.", axes[hoveredIndex].name);
        }

        int drawOrder[3] = {0, 1, 2};
        std::sort(drawOrder, drawOrder + 3, [&](int a, int b)
                  { return axes[a].depth > axes[b].depth; });

        auto withAlpha = [](ImU32 color, int alpha)
        {
            return (color & IM_COL32(255, 255, 255, 0)) | IM_COL32(0, 0, 0, alpha);
        };

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        ImFont *font = ImGui::GetFont();
        const float fontSize = ImGui::GetFontSize();
        drawList->AddCircleFilled(center, 4.0f, IM_COL32(22, 24, 28, 150));
        drawList->AddCircle(center, 4.0f, IM_COL32(255, 255, 255, 120), 12, 1.0f);

        for (int orderIndex : drawOrder)
        {
            const OrientationAxis &axis = axes[orderIndex];
            const int alpha = axis.hovered ? 255 : (axis.depth > 0.0f ? 170 : 230);
            const ImU32 color = withAlpha(axis.color, alpha);
            const float thickness = axis.hovered ? 4.0f : 3.0f;
            ImVec2 labelPos = axis.end;

            if (axis.projectedLength > 0.12f)
            {
                const float headLength = axis.hovered ? 13.0f : 11.0f;
                const float headWidth = axis.hovered ? 8.0f : 7.0f;
                const ImVec2 shaftEnd(axis.end.x - axis.dir2.x * headLength * 0.75f,
                                      axis.end.y - axis.dir2.y * headLength * 0.75f);
                const ImVec2 perp(-axis.dir2.y, axis.dir2.x);
                const ImVec2 left(axis.end.x - axis.dir2.x * headLength + perp.x * headWidth,
                                  axis.end.y - axis.dir2.y * headLength + perp.y * headWidth);
                const ImVec2 right(axis.end.x - axis.dir2.x * headLength - perp.x * headWidth,
                                   axis.end.y - axis.dir2.y * headLength - perp.y * headWidth);

                drawList->AddLine(center, shaftEnd, color, thickness);
                drawList->AddTriangleFilled(axis.end, left, right, color);
                labelPos = ImVec2(axis.end.x + axis.dir2.x * 9.0f, axis.end.y + axis.dir2.y * 9.0f);
            }
            else
            {
                const float dotRadius = axis.hovered ? 8.5f : 7.0f;
                drawList->AddCircleFilled(axis.end, dotRadius, color, 18);
                drawList->AddCircle(axis.end, dotRadius, IM_COL32(255, 255, 255, axis.hovered ? 210 : 130), 18, 1.0f);
                labelPos = ImVec2(axis.end.x + 10.0f, axis.end.y - 10.0f);
            }

            const ImVec2 labelSize = ImGui::CalcTextSize(axis.letter);
            labelPos.x = glm::clamp(labelPos.x - labelSize.x * 0.5f, imageMin.x + 2.0f, imageMin.x + imageSize.x - labelSize.x - 2.0f);
            labelPos.y = glm::clamp(labelPos.y - labelSize.y * 0.5f, imageMin.y + 2.0f, imageMin.y + imageSize.y - labelSize.y - 2.0f);
            drawList->AddText(font, fontSize, ImVec2(labelPos.x + 1.0f, labelPos.y + 1.0f), IM_COL32(0, 0, 0, alpha), axis.letter);
            drawList->AddText(font, fontSize, labelPos, color, axis.letter);
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
        s_orientationGizmoHovered = false;
        s_orientationGizmoActive = false;
        s_stripIkGizmoHovered = false;
        s_stripIkGizmoActive = false;

        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 imageMax(imageMin.x + imageSize.x, imageMin.y + imageSize.y);

        dl->PushClipRect(imageMin, imageMax, true);

        ImGuizmo::BeginFrame();

        const bool runtimeUiGizmoActive = GUIState::s_useTransformGizmo && DrawRuntimeUiTransformGizmo(imageMin, imageSize);
        if (!runtimeUiGizmoActive)
        {
            DrawSkinnedStrip2DIkGizmo(imageMin, imageSize);
            if (GUIState::s_useTransformGizmo && !s_stripIkGizmoHovered && !s_stripIkGizmoActive)
                DrawTransformGizmo(imageMin, imageSize);
        }

        if (GUIState::s_useLightGizmos)
            DrawLightGizmos(imageMin, imageSize);

        DrawPostProcessVolumeGizmos(imageMin, imageSize);
        DrawCameraGizmos(imageMin, imageSize);
        if (GUIState::s_useOrientationGizmo)
            DrawOrientationGizmo(imageMin, imageSize);

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
        ui::ItemTooltip("Select this viewport gizmo.");
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
            if (!scene.IsNodeHierarchyEnabled(scene.GetPointLights()[i].nodeId))
                continue;
            if (checkGizmoIcon(vec3(scene.GetPointLights()[i].position), ICON_FA_LIGHTBULB, LightType::Point, i))
                drawLightVisuals(vec3(scene.GetPointLights()[i].position), LightType::Point, i);
        }

        // Spot Lights
        for (int i = 0; i < (int)scene.GetSpotLights().size(); i++)
        {
            if (!scene.IsNodeHierarchyEnabled(scene.GetSpotLights()[i].nodeId))
                continue;
            if (checkGizmoIcon(vec3(scene.GetSpotLights()[i].position), ICON_FA_LIGHTBULB, LightType::Spot, i))
                drawLightVisuals(vec3(scene.GetSpotLights()[i].position), LightType::Spot, i);
        }

        // Directional Lights
        for (int i = 0; i < (int)scene.GetDirectionalLights().size(); i++)
        {
            if (!scene.IsNodeHierarchyEnabled(scene.GetDirectionalLights()[i].nodeId))
                continue;
            if (checkGizmoIcon(vec3(scene.GetDirectionalLights()[i].position), ICON_FA_SUN, LightType::Directional, i))
                drawLightVisuals(vec3(scene.GetDirectionalLights()[i].position), LightType::Directional, i);
        }

        // Area Lights
        for (int i = 0; i < (int)scene.GetAreaLights().size(); i++)
        {
            if (!scene.IsNodeHierarchyEnabled(scene.GetAreaLights()[i].nodeId))
                continue;
            if (checkGizmoIcon(vec3(scene.GetAreaLights()[i].position), ICON_FA_LIGHTBULB, LightType::Area, i))
                drawLightVisuals(vec3(scene.GetAreaLights()[i].position), LightType::Area, i);
        }
    }

    void SceneView::DrawPostProcessVolumeGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize)
    {
        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        if (!renderer)
            return;
        Scene &scene = renderer->GetScene();
        Camera *camera = scene.GetActiveCamera();
        if (!camera)
            return;

        const mat4 viewProj = camera->GetProjectionNoJitter() * camera->GetView();
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        auto &sel = SelectionManager::Instance();

        // Unit-cube corners (bit0=x, bit1=y, bit2=z) and the 12 edges between adjacent corners.
        static const int kEdges[12][2] = {
            {0, 1}, {0, 2}, {0, 4}, {1, 3}, {1, 5}, {2, 3}, {2, 6}, {3, 7}, {4, 5}, {4, 6}, {5, 7}, {6, 7}};

        auto drawBox = [&](const mat4 &world, ImU32 color, float thickness)
        {
            ImVec2 screen[8];
            bool ok[8];
            for (int c = 0; c < 8; ++c)
            {
                const vec3 local((c & 1) ? 0.5f : -0.5f, (c & 2) ? 0.5f : -0.5f, (c & 4) ? 0.5f : -0.5f);
                const vec4 wp = world * vec4(local, 1.0f);
                ok[c] = ProjectWorldToViewport(vec3(wp.x, wp.y, wp.z), viewProj, imageMin, imageSize, screen[c]);
            }
            for (const auto &e : kEdges)
                if (ok[e[0]] && ok[e[1]])
                    drawList->AddLine(screen[e[0]], screen[e[1]], color, thickness);
        };

        // All bounded volume types share the box gizmo (global volumes are unbounded -> no box):
        // post-process = cyan, trigger = yellow.
        for (uint32_t i = 0; i < scene.GetNodeCount(); ++i)
        {
            NodeId *node = scene.GetNodeId(i);
            if (!scene.IsNodeHierarchyEnabled(node))
                continue;
            const bool selected = sel.GetSelectionType() == SelectionType::Node && sel.GetSelectedNode() == node;
            const float thickness = selected ? 3.0f : 2.0f;

            if (NodePostProcessVolumeTag *vol = scene.GetPostProcessVolumeForNode(node); vol && !vol->global)
                drawBox(scene.GetWorldMatrix(node),
                        selected ? IM_COL32(130, 225, 255, 255) : IM_COL32(110, 195, 235, 205), thickness);

            if (scene.GetTriggerVolumeForNode(node))
                drawBox(scene.GetWorldMatrix(node),
                        selected ? IM_COL32(255, 225, 120, 255) : IM_COL32(235, 195, 90, 205), thickness);
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
            if (camNode && !scene.IsNodeHierarchyEnabled(camNode))
                continue;

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
                float farZ = camera->IsOrthographic() ? 0.0f : (isInfinite ? 0.00001f : (nearPlane / camera->GetFarPlane()));

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
