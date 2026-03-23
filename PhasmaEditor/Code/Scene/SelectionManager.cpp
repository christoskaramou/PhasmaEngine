#include "SelectionManager.h"

namespace pe
{
    SelectionManager &SelectionManager::Instance()
    {
        static SelectionManager instance;
        return instance;
    }

    void SelectionManager::Select(NodeId *node, SelectionType type)
    {
        m_selectedNode = node;
        m_selectionType = type;

        m_selectedLightIndex = -1;
        m_selectedEmitterIndex = -1;
        m_selectedCameraIndex = -1;
    }

    void SelectionManager::Select(LightType type, int index)
    {
        m_selectedNode = nullptr;
        m_selectedEmitterIndex = -1;
        m_selectedCameraIndex = -1;

        m_selectedLightType = type;
        m_selectedLightIndex = index;
        m_selectionType = SelectionType::Light;
    }

    void SelectionManager::SelectCamera(int index)
    {
        m_selectedNode = nullptr;
        m_selectedLightIndex = -1;
        m_selectedEmitterIndex = -1;

        m_selectedCameraIndex = index;
        m_selectionType = SelectionType::Camera;
    }

    void SelectionManager::SelectEmitter(int index)
    {
        m_selectedNode = nullptr;
        m_selectedLightIndex = -1;
        m_selectedCameraIndex = -1;

        m_selectedEmitterIndex = index;
        m_selectionType = SelectionType::Emitter;
    }

    void SelectionManager::ClearSelection()
    {
        m_selectedNode = nullptr;
        m_selectedLightIndex = -1;
        m_selectedEmitterIndex = -1;
        m_selectedCameraIndex = -1;
    }

    bool SelectionManager::HasSelection() const
    {
        return m_selectedNode || m_selectedLightIndex != -1 || m_selectedEmitterIndex != -1 || m_selectedCameraIndex != -1;
    }

    NodeId *SelectionManager::GetSelectedNode() const
    {
        return m_selectedNode;
    }

    SelectionType SelectionManager::GetSelectionType() const
    {
        return m_selectionType;
    }

    LightType SelectionManager::GetSelectedLightType() const
    {
        return m_selectedLightType;
    }

    int SelectionManager::GetSelectedLightIndex() const
    {
        return m_selectedLightIndex;
    }

    int SelectionManager::GetSelectedEmitterIndex() const
    {
        return m_selectedEmitterIndex;
    }

    int SelectionManager::GetSelectedCameraIndex() const
    {
        return m_selectedCameraIndex;
    }

    GizmoOperation SelectionManager::GetGizmoOperation() const
    {
        return m_gizmoOperation;
    }

    void SelectionManager::SetGizmoOperation(GizmoOperation op)
    {
        m_gizmoOperation = op;
    }
} // namespace pe
