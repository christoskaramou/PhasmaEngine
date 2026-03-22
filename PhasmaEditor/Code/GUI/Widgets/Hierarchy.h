#pragma once
#include "GUI/Widget.h"

namespace pe
{
    class ModelAsset;

    class Hierarchy : public Widget
    {
    public:
        Hierarchy();
        void Update() override;

    private:
        ModelAsset *m_lastSelectedModel = nullptr;
        int m_lastSelectedNodeIndex = -1;

        // For auto-expansion
        ModelAsset *m_modelToExpand = nullptr;
        std::unordered_set<int> m_nodesToExpand;
        bool m_scrollToSelection = false;
    };
} // namespace pe
