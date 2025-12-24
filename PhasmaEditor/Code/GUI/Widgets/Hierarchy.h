#pragma once
#include "GUI/Widget.h"

namespace pe
{
    class Hierarchy : public Widget
    {
    public:
        Hierarchy();
        void Update() override;

        size_t GetSelectedId() const { return m_selectedId; }
        int GetSelectedNodeIndex() const { return m_selectedNodeIndex; }

    private:
        size_t m_selectedId = (size_t)-1;
        int m_selectedNodeIndex = -1;
    };
} // namespace pe
