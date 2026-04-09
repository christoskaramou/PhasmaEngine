#pragma once
#include "GUI/Widget.h"
#include "GUI/AI/AICompletionService.h"
#include <chrono>

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

        void SetCompletionService(AICompletionService *svc) { m_completion = svc; }

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
        void ApplyLocalTransform(NodeId *node, const float t[3], const float r[3], const float s[3]);

        AICompletionService *m_completion = nullptr;
        std::string m_hoverFieldId;
        std::chrono::steady_clock::time_point m_hoverStart;
        std::string m_pendingSuggestion;
        std::string m_suggestionFieldId;
        bool m_aiRequestInFlight = false;

        // Returns true if user accepted the suggestion (wrote outAccepted)
        bool TryAISuggestion(const std::string &fieldId,
                             const std::string &nodeType,
                             const std::string &fieldName,
                             const std::string &currentValue,
                             std::string &outAccepted);
    };
} // namespace pe
