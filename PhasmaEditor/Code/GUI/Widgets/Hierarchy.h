#pragma once
#include "GUI/Widget.h"

namespace pe
{
    struct NodeId;

    class Hierarchy : public Widget
    {
    public:
        Hierarchy();
        void Update() override;

    private:
        NodeId *m_lastSelectedNode = nullptr;

        // For auto-expansion
        NodeId *m_nodeToExpand = nullptr;
        std::unordered_set<NodeId *> m_nodesToExpand;
        bool m_scrollToSelection = false;
    };
} // namespace pe
