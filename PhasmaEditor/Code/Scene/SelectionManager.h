#pragma once

namespace pe
{
    struct NodeId;

    enum class GizmoOperation
    {
        Translate,
        Rotate,
        Scale
    };

    enum class SelectionType
    {
        Node,
        Mesh,
        Camera,
        Light,
        Emitter
    };

    enum class LightType
    {
        Directional,
        Point,
        Spot,
        Area
    };

    class SelectionManager
    {
    public:
        static SelectionManager &Instance();

        void Select(NodeId *node, SelectionType type = SelectionType::Node);
        void Select(LightType type, int index);
        void SelectCamera(int index);
        void SelectEmitter(int index);
        void ClearSelection();

        bool HasSelection() const;
        NodeId *GetSelectedNode() const;
        SelectionType GetSelectionType() const;

        LightType GetSelectedLightType() const;
        int GetSelectedLightIndex() const;
        int GetSelectedEmitterIndex() const;
        int GetSelectedCameraIndex() const;

        GizmoOperation GetGizmoOperation() const;
        void SetGizmoOperation(GizmoOperation op);

    private:
        SelectionManager() = default;

        NodeId *m_selectedNode = nullptr;

        LightType m_selectedLightType = LightType::Directional;
        int m_selectedLightIndex = -1;

        int m_selectedEmitterIndex = -1;
        int m_selectedCameraIndex = -1;
        SelectionType m_selectionType = SelectionType::Node;
        GizmoOperation m_gizmoOperation = GizmoOperation::Translate;
    };
} // namespace pe
