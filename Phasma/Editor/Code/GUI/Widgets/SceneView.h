#pragma once
#include "GUI/Widget.h"
#include "imgui/imgui.h"

namespace pe
{
    struct NodeId;

    class SceneView : public Widget
    {
    public:
        SceneView() : Widget("Viewport") {}
        void Init(GUI *gui) override;
        void Update() override;

    private:
        // cycle: a repeated click on the selection steps to the next hit behind it; right-click keeps it.
        NodeId *PerformObjectPicking(float normalizedX, float normalizedY, bool cycle = true);
        void OpenAgentContextMenu(float normalizedX, float normalizedY);
        void DrawAgentContextMenu();

        NodeId *m_contextNode = nullptr;
        void DrawGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize);
        bool DrawRuntimeUiTransformGizmo(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void DrawSkinnedStrip2DIkGizmo(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void DrawTransformGizmo(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void DrawOrientationGizmo(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void DrawLightGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void DrawVolumeGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void DrawNavMeshGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void DrawProbeGridGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void DrawProbeInfluence(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void DrawAudioGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void DrawEmitterGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void DrawPhysics2DGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void DrawCameraGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize);
        bool DrawGizmoIcon(const vec3 &pos, const char *icon, const mat4 &viewProj, const ImVec2 &imageMin, const ImVec2 &imageSize, bool isSelected, const char *id);
    };
} // namespace pe
