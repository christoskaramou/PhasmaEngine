#pragma once
#include "GUI/Widget.h"

namespace pe
{
    struct NodeId;

    class TransformWidget : public Widget
    {
    public:
        TransformWidget();
        ~TransformWidget();
        void Init(GUI *gui) override;
        void Update() override;
        void DrawEmbed(NodeId *node);

    private:
        void DrawNodeInfo(NodeId *node);
        void DrawPositionEditor(NodeId *node);
        void DrawRotationEditor(NodeId *node);
        void DrawScaleEditor(NodeId *node);
        enum class TransformType
        {
            Position,
            Rotation,
            Scale
        };
        void DrawGizmoModeButtons();
        void DrawVec3Control(TransformType type, vec3 &values, float resetValue = 0.0f, float columnWidth = 100.0f);
        void ApplyLocalTransform(NodeId *node, const vec3 &pos, const quat &rot, const vec3 &scl);
    };
} // namespace pe
