#pragma once

#include "Scene/LightTypes.h"

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
        bool IsGizmoLocal() const { return m_gizmoLocal; }
        void SetGizmoLocal(bool local) { m_gizmoLocal = local; }
        // Ctrl-drag snap step per operation: metres, degrees, scale factor.
        float &GizmoSnap(GizmoOperation op) { return m_gizmoSnap[static_cast<int>(op)]; }
        // Runtime UI elements snap their rect to an absolute grid of this many surface pixels.
        float &UiGizmoSnap() { return m_uiGizmoSnap; }

    private:
        SelectionManager() = default;

        NodeId *m_selectedNode = nullptr;

        LightType m_selectedLightType = LightType::Directional;
        int m_selectedLightIndex = -1;

        int m_selectedEmitterIndex = -1;
        int m_selectedCameraIndex = -1;
        SelectionType m_selectionType = SelectionType::Node;
        GizmoOperation m_gizmoOperation = GizmoOperation::Translate;
        bool m_gizmoLocal = false;
        float m_gizmoSnap[3] = {0.5f, 15.f, 0.1f};
        float m_uiGizmoSnap = 8.f;
    };
} // namespace pe
