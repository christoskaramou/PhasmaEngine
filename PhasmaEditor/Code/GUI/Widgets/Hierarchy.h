#pragma once
#include "GUI/Widget.h"

namespace pe
{
    class Model;

    class Hierarchy : public Widget
    {
    public:
        Hierarchy();
        void Update() override;

    private:
        Model *m_lastSelectedModel = nullptr;
        int m_lastSelectedNodeIndex = -1;

        // For auto-expansion
        Model *m_modelToExpand = nullptr;
        std::unordered_set<int> m_nodesToExpand;
        bool m_scrollToSelection = false;
    };
} // namespace pe
