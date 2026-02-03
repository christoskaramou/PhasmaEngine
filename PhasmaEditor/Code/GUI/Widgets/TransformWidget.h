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
        void DrawVec3Control(TransformType type, vec3 &values, float resetValue = 0.0f, float columnWidth = 50.0f);
        void ApplyLocalTransform(NodeInfo *nodeInfo, const vec3 &pos, const vec3 &rot, const vec3 &scl);

        void *m_translateIcon = nullptr;
        void *m_rotateIcon = nullptr;
        void *m_scaleIcon = nullptr;

        Image *m_translateImage = nullptr;
        Image *m_rotateImage = nullptr;
        Image *m_scaleImage = nullptr;
    };
} // namespace pe
