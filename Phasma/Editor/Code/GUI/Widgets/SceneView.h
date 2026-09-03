#pragma once
#include "GUI/Widget.h"
#include "imgui/imgui.h"

namespace pe
{
    class SceneView : public Widget
    {
    public:
        SceneView() : Widget("Viewport") {}
        void Init(GUI *gui) override;
        void Update() override;

    private:
        void PerformObjectPicking(float normalizedX, float normalizedY);
        void DrawGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize);
        bool DrawRuntimeUiTransformGizmo(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void DrawSkinnedStrip2DIkGizmo(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void DrawTransformGizmo(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void DrawOrientationGizmo(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void DrawLightGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void DrawTriggerZoneGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void DrawCameraGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize);
        bool DrawGizmoIcon(const vec3 &pos, const char *icon, const mat4 &viewProj, const ImVec2 &imageMin, const ImVec2 &imageSize, bool isSelected, const char *id);
    };
} // namespace pe
