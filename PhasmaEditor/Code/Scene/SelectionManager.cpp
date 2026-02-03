#include "SelectionManager.h"
#include "Model.h"

namespace pe
{
    SelectionManager &SelectionManager::Instance()
    {
        static SelectionManager instance;
        return instance;
    }

    void SelectionManager::Select(Model *model, int nodeIndex, SelectionType type)
    {
        m_selectedModel = model;
        m_selectedNodeIndex = nodeIndex;
        m_selectionType = type;

        m_selectedLightIndex = -1; // Deselect light
    }

    void SelectionManager::Select(LightType type, int index)
    {
        m_selectedModel = nullptr; // Deselect model
        m_selectedNodeIndex = -1;

        m_selectedLightType = type;
        m_selectedLightIndex = index;
        m_selectionType = SelectionType::Light;
    }

    void SelectionManager::ClearSelection()
    {
        m_selectedModel = nullptr;
        m_selectedNodeIndex = -1;
        m_selectedLightIndex = -1;
    }

    bool SelectionManager::HasSelection() const
    {
        return m_selectedModel || m_selectedLightIndex != -1 || m_selectionType == SelectionType::Camera;
    }

    Model *SelectionManager::GetSelectedModel() const
    {
        return m_selectedModel;
    }

    int SelectionManager::GetSelectedNodeIndex() const
    {
        return m_selectedNodeIndex;
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

    NodeInfo *SelectionManager::GetSelectedNodeInfo()
    {
        if (!HasSelection())
            return nullptr;

        auto &nodeInfos = m_selectedModel->GetNodeInfos();
        if (m_selectedNodeIndex < 0 || m_selectedNodeIndex >= static_cast<int>(nodeInfos.size()))
            return nullptr;

        return &nodeInfos[m_selectedNodeIndex];
    }

    const NodeInfo *SelectionManager::GetSelectedNodeInfo() const
    {
        if (!HasSelection())
            return nullptr;

        const auto &nodeInfos = m_selectedModel->GetNodeInfos();
        if (m_selectedNodeIndex < 0 || m_selectedNodeIndex >= static_cast<int>(nodeInfos.size()))
            return nullptr;

        return &nodeInfos[m_selectedNodeIndex];
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
