#pragma once
#include "GUI/Widget.h"

namespace pe
{
    struct NodeInfo;

    class TransformWidget : public Widget
    {
    public:
        TransformWidget();
        ~TransformWidget();
        void Init(GUI *gui) override;
        void Update() override;
        void DrawEmbed(NodeInfo *node);

    private:
        void DrawNodeInfo(NodeInfo *node);
        void DrawPositionEditor(NodeInfo *node);
        void DrawRotationEditor(NodeInfo *node);
        void DrawScaleEditor(NodeInfo *node);
        enum class TransformType
        {
            Position,
            Rotation,
            Scale
        };
        void DrawGizmoModeButtons();
        void DrawVec3Control(TransformType type, vec3 &values, float resetValue = 0.0f, float columnWidth = 100.0f);
        void ApplyLocalTransform(NodeInfo *nodeInfo, const vec3 &pos, const quat &rot, const vec3 &scl);
    };
} // namespace pe
