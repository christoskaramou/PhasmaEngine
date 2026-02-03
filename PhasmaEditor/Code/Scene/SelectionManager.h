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

    enum class SelectionType
    {
        Node,
        Mesh
    };

    class SelectionManager
    {
    public:
        static SelectionManager &Instance();

        void Select(Model *model, int nodeIndex, SelectionType type = SelectionType::Node);
        void ClearSelection();

        bool HasSelection() const;
        Model *GetSelectedModel() const;
        int GetSelectedNodeIndex() const;
        SelectionType GetSelectionType() const;
        NodeInfo *GetSelectedNodeInfo();
        const NodeInfo *GetSelectedNodeInfo() const;

        GizmoOperation GetGizmoOperation() const;
        void SetGizmoOperation(GizmoOperation op);

    private:
        SelectionManager() = default;

        Model *m_selectedModel = nullptr;
        int m_selectedNodeIndex = -1;
        SelectionType m_selectionType = SelectionType::Node;
        GizmoOperation m_gizmoOperation = GizmoOperation::Translate;
    };
} // namespace pe
