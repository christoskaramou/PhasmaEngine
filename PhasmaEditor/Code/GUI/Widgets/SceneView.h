#pragma once
#include "GUI/Widget.h"
#include "imgui/imgui.h"

namespace pe
{
    class SceneView : public Widget
    {
    public:
        SceneView() : Widget("Scene View") {}
        void Init(GUI *gui) override;
        void Update() override;

    private:
        void PerformObjectPicking(float normalizedX, float normalizedY);
        void DrawGizmo(const ImVec2 &imageMin, const ImVec2 &imageSize);
        void ApplyTransformToNode(class NodeInfo *nodeInfo, const mat4 &newWorldMatrix);
    };
} // namespace pe
