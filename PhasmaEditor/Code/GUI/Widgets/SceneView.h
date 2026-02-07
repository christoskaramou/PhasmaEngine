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
        void DrawTransformGizmo(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void DrawLightGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void DrawCameraGizmos(const ImVec2 &imageMin, const ImVec2 &imageSize);
        bool DrawGizmoIcon(const vec3 &pos, const char *icon, const mat4 &viewProj, const ImVec2 &imageMin, const ImVec2 &imageSize, bool isSelected, const char *id);
        void ApplyTransformToNode(class NodeInfo *nodeInfo, const mat4 &newWorldMatrix);
    };
} // namespace pe
