#pragma once

namespace pe
{
    class Model;
    struct NodeInfo;

    enum class GizmoOperation
    {
        Translate,
        Rotate,
        Scale
    };

    class SelectionManager
    {
    public:
        static SelectionManager &Instance();

        void Select(Model *model, int nodeIndex);
        void ClearSelection();

        bool HasSelection() const;
        Model *GetSelectedModel() const;
        int GetSelectedNodeIndex() const;
        NodeInfo *GetSelectedNodeInfo();
        const NodeInfo *GetSelectedNodeInfo() const;

        GizmoOperation GetGizmoOperation() const;
        void SetGizmoOperation(GizmoOperation op);

    private:
        SelectionManager() = default;

        Model *m_selectedModel = nullptr;
        int m_selectedNodeIndex = -1;
        GizmoOperation m_gizmoOperation = GizmoOperation::Translate;
    };
} // namespace pe
